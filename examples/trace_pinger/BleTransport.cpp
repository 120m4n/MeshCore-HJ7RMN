#include "BleTransport.h"

#include "tracecore/LineCodec.h"

namespace {
volatile bool g_disconnected = false;   // la levanta el callback (otra tarea); la consume loop()

void onDisconnect(uint16_t /*conn_handle*/, uint8_t /*reason*/) { g_disconnected = true; }
}  // namespace

void BleTransport::begin() {
  Bluefruit.autoConnLed(false);
  // MTU 247 (iOS suele negociar 185). Debe pedirse antes de begin().
  Bluefruit.configPrphConn(247, BLE_GAP_EVENT_LENGTH_DEFAULT, BLE_GATTS_HVN_TX_QUEUE_SIZE_DEFAULT,
                           BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT);
  Bluefruit.begin(1, 0);   // una sola conexión periférica (FR-006)
  Bluefruit.setTxPower(0);

  const ble_gap_addr_t addr = Bluefruit.getAddr();
  char name[24];
  snprintf(name, sizeof name, "YagiPinger-%02X%02X", addr.addr[1], addr.addr[0]);   // últimos 2 bytes de la dirección
  Bluefruit.setName(name);

  Bluefruit.Periph.setConnInterval(12, 24);   // 15–30 ms (unidades de 1.25 ms)
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  bleuart_.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart_);   // UUID de NUS en el advertising
  Bluefruit.ScanResponse.addName();             // el nombre completo va en el scan response
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 800);  // 100 ms los primeros 30 s, luego 500 ms (unidades de 0.625 ms)
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

bool BleTransport::ready() { return Bluefruit.connected() && bleuart_.notifyEnabled(); }

void BleTransport::line(const char* s, size_t n) {
  if (!ready() || n >= tracecore::kMaxLine) return;
  uint8_t buf[tracecore::kMaxLine + 1];
  memcpy(buf, s, n);
  buf[n] = '\n';
  bleuart_.write(buf, n + 1);
}

bool BleTransport::takeDisconnected() {
  if (!g_disconnected) return false;
  g_disconnected = false;
  assembler_.reset();
  return true;
}
