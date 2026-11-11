// Catcher para XIAO nRF52840 + Wio-SX1262 o Faketec (ProMicro nRF52840 + SX1262).
// Mide el nivel de una baliza CW: MEAS lee RSSI instantáneo (0,5 dB) en la frecuencia actual y devuelve la
// estadística filtrada. No transmite nunca. Cascarón Arduino: traduce radio, USB serie y BLE a tracecore::Catcher.
#include <Arduino.h>
#include <target.h>

#include <string_view>

#include "BleTransport.h"
#include "CompositeSink.h"
#include "tracecore/Catcher.h"
#include "tracecore/LineAssembler.h"
#include "tracecore/LineCodec.h"

using namespace tracecore;

namespace {

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

UsbSink usb;
BleTransport ble;
CompositeSink sink(usb, ble);   // los eventos salen por Serial y por BLE
Catcher* catcher = nullptr;
LineAssembler serialLines;

constexpr uint32_t kMeasGapUs = 2000;   // ~1 lectura nueva por paso; 64 lecturas ≈ 130 ms

void applyRadio(const RadioConfig& r) {
  radio_driver.setParams(r.freqMhz(), r.bwKhz(), r.sf, r.cr);
  radio_driver.onSendFinished();   // radio a reposo; recvRaw() la vuelve a armar en RX
}

// n lecturas de RSSI instantáneo con filtro robusto de ráfagas ajenas (rssiStats).
void measure(size_t n) {
  uint8_t pkt[256];
  radio_driver.recvRaw(pkt, sizeof pkt);   // arma RX si un SET acaba de dejar la radio en reposo (un paquete ajeno se pierde)
  delay(5);
  int16_t v[kMeasMax];
  for (size_t i = 0; i < n; i++) {
    v[i] = (int16_t)lroundf(radio_driver.getCurrentRSSI() * 2);
    delayMicroseconds(kMeasGapUs);
  }
  RssiStats r;
  if (rssiStats(v, n, r)) catcher->emitMeas(r);
}

void command(std::string_view l) {
  const Action a = catcher->onCommand(l);
  if (a.kind == Action::Reconfigure) applyRadio(catcher->config());
  else if (a.kind == Action::Measure) measure(a.n);
}

void pollSerial() {
  while (Serial.available()) {
    switch (serialLines.push((uint8_t)Serial.read())) {
      case LineAssembler::Result::Line:
        command(std::string_view(serialLines.line(), serialLines.length()));
        break;
      case LineAssembler::Result::Overflow: sink.line("!ERR syntax", 11); break;
      default: break;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(100);
  board.begin();
  if (!radio_init()) {
    Serial.println("!ERR radio init");
    while (1) delay(1000);
  }
  radio_driver.begin();

  char ble_name[24];
  snprintf(ble_name, sizeof ble_name, "PingPong-CA-%08lX", (unsigned long)NRF_FICR->DEVICEID[0]);   // los 6 hex finales = id del #HELLO
  ble.begin(ble_name);

  const RadioConfig cfg;   // 915.800; la app fija la frecuencia de la baliza al empezar
  applyRadio(cfg);
  catcher = new Catcher(cfg, sink, NRF_FICR->DEVICEID[0]);
  catcher->emitBoot();
  board.onBootComplete();
}

void loop() {
  pollSerial();
  ble.poll([](std::string_view l) { command(l); }, []() { sink.line("!ERR syntax", 11); });
  uint8_t buf[256];
  radio_driver.recvRaw(buf, sizeof buf);   // mantiene RX armado y descarta paquetes ajenos
}
