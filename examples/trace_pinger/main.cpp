// Pinger TRACE para XIAO nRF52840 + Wio-SX1262 (spec 001-trace-pinger-firmware).
// Cascarón Arduino: traduce radio, USB serie y flash a eventos de tracecore::Pinger.
// No ejecuta el core de mesh: solo transmite tramas TRACE/DIRECT (FR-008).
#include <Arduino.h>
#include <InternalFileSystem.h>
#include <target.h>

#include <string_view>

#include "tracecore/Airtime.h"
#include "tracecore/LineCodec.h"
#include "tracecore/Pinger.h"

using namespace Adafruit_LittleFS_Namespace;
using namespace tracecore;

namespace {

constexpr const char* kCfgPath = "/pinger.cfg";
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

enum class CfgLoad { Loaded, Missing, Corrupt };

CfgLoad cfgLoad(PingerConfig& out) {
  if (!InternalFS.exists(kCfgPath)) return CfgLoad::Missing;
  File f = InternalFS.open(kCfgPath);
  if (!f) return CfgLoad::Corrupt;
  uint8_t b[kCfgBlobSize];
  const int n = f.read(b, sizeof b);
  f.close();
  const auto c = deserialize(b, n < 0 ? 0 : (size_t)n);
  if (!c) return CfgLoad::Corrupt;
  out = *c;
  return CfgLoad::Loaded;
}

bool cfgSave(const PingerConfig& c) {
  uint8_t b[kCfgBlobSize];
  serialize(c, b);
  InternalFS.remove(kCfgPath);  // FILE_O_WRITE no trunca
  File f = InternalFS.open(kCfgPath, FILE_O_WRITE);
  if (!f) return false;
  const size_t w = f.write(b, sizeof b);
  f.close();
  return w == sizeof b;
}

HwRng rng;
UsbSink sink;
Pinger* pinger = nullptr;
bool tx_wait = false;
uint32_t tx_t0 = 0;
char line_buf[kMaxLine];
size_t line_len = 0;
bool line_overflow = false;

void applyRadio(const RadioConfig& r) {
  radio_driver.setParams(r.freqMhz(), r.bwKhz(), r.sf, r.cr);
  radio_driver.setTxPower(r.txp_dbm);
  radio_driver.resetAGC();  // fuerza re-armar RX con los parámetros nuevos
}

void exec(Action a) {
  switch (a.kind) {
    case Action::Transmit: {
      if (a.len != kRequestLen || a.bytes[0] != 0x26) return;  // FR-008: solo TRACE/DIRECT
      const uint32_t now = millis();
      if (radio_driver.isReceiving()) {  // LBT: preámbulo en curso o CAD
        exec(pinger->onChannelBusy(now));
        return;
      }
      if (radio_driver.startSendRaw(a.bytes, a.len)) {
        tx_wait = true;
        tx_t0 = now;
      } else {
        exec(pinger->onTxDone(now, false));
      }
      return;
    }
    case Action::Reconfigure:
      applyRadio(pinger->config().radio);
      return;
    case Action::Save:
      pinger->onSaveDone(cfgSave(pinger->config()));
      return;
    default:  // StartRx / Standby: la radio siempre escucha; recvRaw() re-arma RX tras cada TX
      return;
  }
}

void pollSerial() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n') {
      if (line_overflow) sink.line("!ERR syntax", 11);
      else exec(pinger->onCommand(std::string_view(line_buf, line_len), millis()));
      line_len = 0;
      line_overflow = false;
    } else if (line_len < sizeof line_buf - 1) {
      line_buf[line_len++] = c;
    } else {
      line_overflow = true;
    }
  }
}

}  // namespace

void setup() {
  board.begin();
  if (!radio_init()) {
    while (1) delay(1000);
  }
  radio_driver.begin();
  randomSeed(radio_driver.getRngSeed());
  InternalFS.begin();

  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(100);

  PingerConfig cfg;  // valores por defecto: preset Australia, txp 22, sin hash
  const CfgLoad loaded = cfgLoad(cfg);
  if (loaded == CfgLoad::Corrupt) cfg = PingerConfig();
  applyRadio(cfg.radio);

  pinger = new Pinger(cfg, rng, sink, NRF_FICR->DEVICEID[0]);
  pinger->emitBoot(loaded == CfgLoad::Corrupt);
  board.onBootComplete();
}

void loop() {
  pollSerial();

  if (tx_wait) {
    if (radio_driver.isSendComplete()) {
      radio_driver.onSendFinished();
      tx_wait = false;
      exec(pinger->onTxDone(millis(), true));
    } else if (millis() - tx_t0 > kTxTimeoutMs) {
      radio_driver.onSendFinished();
      tx_wait = false;
      exec(pinger->onTxDone(millis(), false));
    }
  } else {
    uint8_t buf[256];
    const int n = radio_driver.recvRaw(buf, sizeof buf);
    if (n > 0) exec(pinger->onRx(buf, (size_t)n, radio_driver.getLastRSSI(), radio_driver.getLastSNR(), millis()));
  }

  exec(pinger->tick(millis()));
}
