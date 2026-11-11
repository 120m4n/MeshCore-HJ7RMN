// BleTransport para ESP32 (Bluedroid, NUS sin emparejar), mismo contrato que BleTransport.cpp (nRF52).
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_mac.h>

#include "BleTransport.h"
#include "tracecore/LineCodec.h"

namespace {
constexpr const char* kNusService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kNusRx = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kNusTx = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

// Cola SPSC: escribe la tarea BT (onWrite), lee loop(). Si se llena, descarta bytes.
constexpr uint16_t kRxSize = 512;
uint8_t g_rx[kRxSize];
volatile uint16_t g_head = 0, g_tail = 0;

volatile bool g_disconnected = false;   // la levanta el callback (otra tarea); la consume loop()
volatile bool g_connected = false;
volatile uint16_t g_conn_id = 0;
BLEServer* g_server = nullptr;
BLECharacteristic* g_tx = nullptr;
BLE2902* g_cccd = nullptr;

struct ServerCb : BLEServerCallbacks {
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* p) override {
    g_conn_id = p->connect.conn_id;
    g_connected = true;
  }
  void onDisconnect(BLEServer* s) override {
    g_connected = false;
    g_disconnected = true;
    s->getAdvertising()->start();   // una sola conexión periférica (FR-006): re-anuncia al soltarla
  }
};

struct RxCb : BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    const uint8_t* d = c->getData();
    const size_t n = c->getLength();
    for (size_t i = 0; i < n; i++) {
      const uint16_t next = (g_head + 1) % kRxSize;
      if (next == g_tail) return;
      g_rx[g_head] = d[i];
      g_head = next;
    }
  }
};
}  // namespace

void BleTransport::begin(const char* name) {
  BLEDevice::init(name);
  BLEDevice::setMTU(247);
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCb());

  BLEService* svc = g_server->createService(kNusService);
  g_tx = svc->createCharacteristic(kNusTx, BLECharacteristic::PROPERTY_NOTIFY);
  g_cccd = new BLE2902();
  g_tx->addDescriptor(g_cccd);
  BLECharacteristic* rx =
      svc->createCharacteristic(kNusRx, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCb());
  svc->start();

  BLEAdvertising* adv = g_server->getAdvertising();
  adv->addServiceUUID(kNusService);   // UUID de NUS en el advertising
  adv->setScanResponse(true);         // el nombre completo va en el scan response
  adv->setMinInterval(160);           // 100 ms (unidades de 0.625 ms); sin el paso a 500 ms del nRF52
  adv->setMaxInterval(160);
  adv->start();
}

bool BleTransport::ready() { return g_connected && g_cccd->getNotifications(); }

void BleTransport::line(const char* s, size_t n) {
  if (!ready() || n >= tracecore::kMaxLine) return;
  uint8_t buf[tracecore::kMaxLine + 1];
  memcpy(buf, s, n);
  buf[n] = '\n';
  // notify solo lleva MTU-3 bytes: trocea como hace BLEUart en el nRF52
  const size_t chunk = g_server->getPeerMTU(g_conn_id) - 3;
  for (size_t off = 0; off <= n; off += chunk) {
    g_tx->setValue(buf + off, min(chunk, n + 1 - off));
    g_tx->notify();
  }
}

int BleTransport::available() { return g_head != g_tail; }

int BleTransport::read() {
  if (g_head == g_tail) return -1;
  const uint8_t b = g_rx[g_tail];
  g_tail = (g_tail + 1) % kRxSize;
  return b;
}

bool BleTransport::takeDisconnected() {
  if (!g_disconnected) return false;
  g_disconnected = false;
  g_tail = g_head;   // descarta lo que quedó a medias
  assembler_.reset();
  return true;
}
