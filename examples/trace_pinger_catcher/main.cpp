// Tarjeta única para XIAO nRF52840 + Wio-SX1262 / Faketec. MODE catcher (mide RSSI, no transmite; arranque) o
// MODE pinger (TRACE DIRECT, 12/15 bytes). Cascarón Arduino: traduce radio, USB serie y BLE a tracecore::Catcher.
#include <Arduino.h>
#ifdef NRF52_PLATFORM
#define DEVICE_ID NRF_FICR->DEVICEID[0]
#else
#define DEVICE_ID ((uint32_t)(ESP.getEfuseMac() >> 16))   // bytes 2..5 de la MAC: los bajos son el OUI de Espressif
#endif
#include <target.h>

#include <string_view>

#include "BleTransport.h"
#include "CompositeSink.h"
#include "tracecore/Airtime.h"
#include "tracecore/TraceFrame.h"
#include "tracecore/LineAssembler.h"
#include "tracecore/LineCodec.h"
#include "tracecore/Catcher.h"

using namespace tracecore;

extern RADIO_CLASS radio;   // target.cpp; para leer GetPacketStatus (PktStatus, más abajo)

namespace {

constexpr uint32_t kTxTimeoutMs = 3000;  // si la radio no avisa fin de TX

// Semilla del TRNG del SX1262, igual que el resto de MeshCore.
struct HwRng : Rng {
  uint32_t next() override { return ((uint32_t)random(0x10000) << 16) | (uint32_t)random(0x10000); }
};

// Cada línea sale en un solo write: el host nunca ve una línea partida.
struct UsbSink : LineSink {
  void line(const char* s, size_t n) override {
    if (!Serial || n >= kMaxLine) return;
    char buf[kMaxLine + 1];
    memcpy(buf, s, n);
    buf[n] = '\n';
    Serial.write((const uint8_t*)buf, n + 1);
  }
};

HwRng rng;
UsbSink usb;
BleTransport ble;
CompositeSink sink(usb, ble);   // los eventos salen por Serial y por BLE
Catcher* catcher = nullptr;
bool tx_wait = false;

// RSSI a 0,5 dB (2026-11-05). El SX1262 da en GetPacketStatus [RssiPkt, SnrPkt, SignalRssiPkt], cada RSSI en −½ dBm.
// RadioLib getRSSI() devuelve SignalRssiPkt, que en los datos llegaba siempre par (pasos de 1 dB). rssi_dn sale ahora
// de RssiPkt (potencia media durante el paquete); RADIO? muestra los dos crudos del último paquete para comprobarlo.
// ponytail: getPacketStatus es protected en RadioLib; se alcanza con un puntero a miembro desde una clase derivada
// para no tocar src/ (upstream). Si RadioLib lo hace público, llamar radio.getPacketStatus() y borrar PktStatus.
struct PktStatus : RADIO_CLASS {
  static uint32_t of(RADIO_CLASS& r) { return (r.*&PktStatus::getPacketStatus)(); }
};
uint8_t last_rssi_pkt = 0, last_sig_rssi_pkt = 0;   // crudos, −½ dBm
uint32_t tx_t0 = 0;
LineAssembler serialLines;

// Radio a reposo (standby) y estado IDLE: el loop re-arma RX con los parámetros actuales (salvo con RF off).
// Sustituye a resetAGC (sleep + wake + calibrate): con varios cambios de radio seguidos (~100 ms) dejaba el SX1262
// con BUSY colgado y el catcher congelado 6-11 s (medido 8/8, 2026-11-05). Mismo camino que MeshCore: setParams sin resetAGC.
void radioIdle() { radio_driver.onSendFinished(); }

void applyRadio(const RadioConfig& r) {
  radio_driver.setParams(r.freqMhz(), r.bwKhz(), r.sf, r.cr);
  radio_driver.setTxPower(min<int>(r.txp_dbm, LORA_TX_POWER));   // SX1276: tope 20 dBm; RadioLib rechaza más
  radioIdle();
}

void exec(Action a) {
  switch (a.kind) {
    case Action::Transmit: {
      // Solo TRACE/DIRECT (12 o 15 bytes): nada más sale por la antena.
      if (!(a.bytes[0] == 0x26 && (a.len == kRequestLen || a.len == kRequestMax))) return;
      const uint32_t now = millis();
      if (radio_driver.isReceiving()) {  // LBT: preámbulo en curso o CAD
        exec(catcher->onChannelBusy(now));
        return;
      }
      // La 1.ª TX tras arrancar fallaba siempre (txerr, verificado al flashear 2026-11-05): si la radio rechaza el
      // inicio, se pasa a standby y se reintenta una vez antes de dar txerr.
      bool started = radio_driver.startSendRaw(a.bytes, a.len);
      if (!started) { radioIdle(); started = radio_driver.startSendRaw(a.bytes, a.len); }
      if (started) {
        tx_wait = true;
        tx_t0 = now;
      } else {
        exec(catcher->onTxDone(now, false));
      }
      return;
    }
    case Action::Reconfigure:
      applyRadio(catcher->config().radio);
      return;
    default:  // StartRx / Standby: la radio siempre escucha; recvRaw() re-arma RX tras cada TX
      return;
  }
}

// MEAS [n]: n lecturas de RSSI instantáneo (GetRssiInst, 0,5 dB) en la frecuencia actual, con filtro robusto de ráfagas
// ajenas (rssiStats). Mide la baliza CW. No transmite. -> M,<n>,<kept>,<mean>,<sd>,<median>,<min>,<max>
constexpr uint32_t kMeasGapUs = 2000;   // ~1 lectura nueva por paso; 64 lecturas ≈ 130 ms

void measNow(size_t n) {
  uint8_t pkt[256];
  radio_driver.recvRaw(pkt, sizeof pkt);   // arma RX si un SET acaba de dejar la radio en standby (un paquete ajeno se pierde)
  delay(5);
  int16_t v[kMeasMax];
  for (size_t i = 0; i < n; i++) {
    v[i] = (int16_t)lroundf(radio_driver.getCurrentRSSI() * 2);
    delayMicroseconds(kMeasGapUs);
  }
  RssiStats r;
  char b[kMaxLine];
  if (rssiStats(v, n, r)) sink.line(b, formatMeas(b, sizeof b, r));
}

void meas(std::string_view arg) {
  size_t n = kMeasDefault;
  if (!arg.empty()) {
    n = 0;
    for (char c : arg) n = (c >= '0' && c <= '9' && n <= 100000) ? n * 10 + (c - '0') : 1000000;
  }
  char b[kMaxLine];
  if (n < kMeasMin || n > kMeasMax) {
    sink.line(b, (size_t)snprintf(b, sizeof b, n >= 1000000 ? "!ERR syntax" : "!ERR range meas %u-%u", (unsigned)kMeasMin, (unsigned)kMeasMax));
    return;
  }
  if (tx_wait || !catcher->idle()) { sink.line("!ERR state busy", 15); return; }
  measNow(n);
}

// ID [s]: parpadeo rápido del LED durante s segundos (5 por defecto, 1-30) para saber qué placa es cuál
// (spec 007-dos-catchers). -> #OK id s=<s>. No toca la radio: se puede pedir en medio de un MEAS.
constexpr uint32_t kIdBlinkMs = 100, kIdMaxS = 30;
uint32_t id_until = 0;

void led(bool on) { digitalWrite(LED_BUILTIN, on ? LED_STATE_ON : !LED_STATE_ON); }

void identify(std::string_view arg) {
  size_t s = arg.empty() ? 5 : 0;
  for (char c : arg) s = (c >= '0' && c <= '9' && s <= kIdMaxS) ? s * 10 + (c - '0') : kIdMaxS + 1;
  if (s < 1 || s > kIdMaxS) { sink.line("!ERR range id 1-30", 18); return; }
  id_until = millis() + s * 1000;
  char b[24];
  sink.line(b, (size_t)snprintf(b, sizeof b, "#OK id s=%u", (unsigned)s));
}

void idTick() {
  if (!id_until) return;
  if ((int32_t)(millis() - id_until) >= 0) { id_until = 0; led(false); return; }
  led((millis() / kIdBlinkMs) & 1);
}

void command(std::string_view l, Origin o) {
  if (l == "ID" || l.substr(0, 3) == "ID ") { identify(l.size() > 3 ? l.substr(3) : std::string_view()); return; }
  if (l == "MEAS" || l.substr(0, 5) == "MEAS ") {
    if (catcher->mode() != Mode::Catcher) sink.line("!ERR mode", 9);   // el pinger no mide
    else meas(l.size() > 5 ? l.substr(5) : std::string_view());
    return;
  }
  exec(catcher->onCommand(l, millis(), o));
}

void pollSerial() {
  while (Serial.available()) {
    switch (serialLines.push((uint8_t)Serial.read())) {
      case LineAssembler::Result::Line:
        command(std::string_view(serialLines.line(), serialLines.length()), Origin::Serial);
        break;
      case LineAssembler::Result::Overflow: sink.line("!ERR syntax", 11); break;
      default: break;
    }
  }
}

void pollBle() {
  ble.poll([](std::string_view l) { command(l, Origin::Ble); },
           []() { sink.line("!ERR syntax", 11); });
  if (ble.takeDisconnected()) exec(catcher->onTransportLost(Origin::Ble));   // detiene GO/ráfagas de BLE
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(100);
  board.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  led(false);
  if (!radio_init()) {
    Serial.println("!ERR radio init");   // p.ej. variante SX1262 flasheada sobre placa SX1276
    while (1) delay(1000);
  }
  radio_driver.begin();
  randomSeed(radio_driver.getRngSeed());
  char ble_name[24];
  snprintf(ble_name, sizeof ble_name, "MeshAz-%08lX", (unsigned long)DEVICE_ID);   // los 6 hex finales = id del #HELLO
  ble.begin(ble_name);

  const CatcherConfig cfg;   // preset Australia, txp 22, sin hash (la app fija hash/freq al conectar); siempre arranca en catcher
  applyRadio(cfg.radio);

  catcher = new Catcher(cfg, rng, sink, DEVICE_ID);
  catcher->emitBoot(false);
  board.onBootComplete();
}

void loop() {
  pollSerial();
  pollBle();
  idTick();

  if (tx_wait) {
    if (radio_driver.isSendComplete()) {
      radio_driver.onSendFinished();
      tx_wait = false;
      exec(catcher->onTxDone(millis(), true));
    } else if (millis() - tx_t0 > kTxTimeoutMs) {
      radio_driver.onSendFinished();
      tx_wait = false;
      exec(catcher->onTxDone(millis(), false));
    }
  } else {
    uint8_t buf[256];
    const int n = radio_driver.recvRaw(buf, sizeof buf);
    if (n > 0) {
      const uint32_t ps = PktStatus::of(radio);
      last_rssi_pkt = (ps >> 16) & 0xFF;
      last_sig_rssi_pkt = ps & 0xFF;
      exec(catcher->onRx(buf, (size_t)n, -0.5f * last_rssi_pkt, radio_driver.getLastSNR(), millis()));
    }
  }

  exec(catcher->tick(millis()));
}
