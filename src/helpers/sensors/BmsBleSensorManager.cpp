#ifdef WITH_BMS_BLE
#include "BmsBleSensorManager.h"
#include "bms_proto.h"
#include <BLEDevice.h>

#ifndef BMS_BLE_NAME
  #define BMS_BLE_NAME "DL"        // name filter used by the SMART BMS app
#endif
#ifndef BMS_INTERVAL_MS
  #define BMS_INTERVAL_MS 3600000UL  // one read per hour, counted from boot
#endif
// A reading counts as current until the next cycle has had time to finish.
#define BMS_MAX_AGE_MS (BMS_INTERVAL_MS + 120000UL)
#ifndef BMS_TELEM_BUDGET
  // ponytail: per-cell voltages stop once the LPP buffer reaches this size, so the reply
  // still fits a path-return packet over a long route. Raise it if your routes are short.
  #define BMS_TELEM_BUDGET 120
#endif

static BLEUUID SVC("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID RX_CHR("0000fff1-0000-1000-8000-00805f9b34fb");  // notify
static BLEUUID TX_CHR("0000fff2-0000-1000-8000-00805f9b34fb");  // write
static BLEUUID HL_SVC("02f00000-0000-0000-0000-00000000fe00");  // Hi-Link module
static BLEUUID HL_KEY("02f00000-0000-0000-0000-00000000ff05");

static portMUX_TYPE bms_mux = portMUX_INITIALIZER_UNLOCKED;
static bms_run_t bms_last;            // guarded by bms_mux
static uint32_t bms_last_ms;          // guarded by bms_mux, 0 = never
static bool cycle_done, cycle_ok;     // guarded by bms_mux
static bms_rx_t bms_rx;               // touched only from the BLE callback

static void onNotify(BLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  bms_run_t r;
  if (!bms_feed(&bms_rx, d, n, &r)) return;
  portENTER_CRITICAL(&bms_mux);
  bms_last = r;
  bms_last_ms = millis() | 1;
  portEXIT_CRITICAL(&bms_mux);
}

static uint32_t lastReadMs() {
  portENTER_CRITICAL(&bms_mux);
  uint32_t at = bms_last_ms;
  portEXIT_CRITICAL(&bms_mux);
  return at;
}

static bool isFresh(uint32_t at) { return at && millis() - at <= BMS_MAX_AGE_MS; }

static void dropClient(BLEClient* c) {
  if (c->isConnected()) {
    c->disconnect();
    // The disconnect event still references the client; wait for it before deleting.
    for (int i = 0; i < 40 && c->isConnected(); i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (c->isConnected()) return;  // ponytail: leaks one client rather than risk a use-after-free
  }
  delete c;
}

// Scan, connect, request one run-data frame, disconnect. True if a new frame arrived.
static bool readOnce() {
  BLEScanResults res = BLEDevice::getScan()->start(5, false);
  BLEAdvertisedDevice* found = NULL;
  for (int i = 0; i < res.getCount() && !found; i++) {
    BLEAdvertisedDevice d = res.getDevice(i);
    if (strstr(d.getName().c_str(), BMS_BLE_NAME)) found = new BLEAdvertisedDevice(d);
  }
  BLEDevice::getScan()->clearResults();
  if (!found) return false;

  BLEClient* c = BLEDevice::createClient();
  bool ok = c->connect(found);
  delete found;
  BLERemoteService* s = ok ? c->getService(SVC) : NULL;
  BLERemoteCharacteristic* rx = s ? s->getCharacteristic(RX_CHR) : NULL;
  BLERemoteCharacteristic* tx = s ? s->getCharacteristic(TX_CHR) : NULL;
  bool got = false;
  if (rx && tx) {
    c->setMTU(512);
    bms_rx.len = 0;
    rx->registerForNotify(onNotify);
    // The app always sends this right after enabling notify (BluetoothRegulate.sendSecretKey).
    BLERemoteService* h = c->getService(HL_SVC);
    BLERemoteCharacteristic* k = h ? h->getCharacteristic(HL_KEY) : NULL;
    if (k) k->writeValue((uint8_t*)"HiLink", 6, true);

    uint32_t before = lastReadMs();
    uint8_t req[8];
    bms_build_read(req, 0, BMS_RUN_REGS);
    for (int attempt = 0; attempt < 3 && !got && c->isConnected(); attempt++) {
      tx->writeValue(req, sizeof(req), true);
      for (int i = 0; i < 30 && !got; i++) {  // up to 3 s per attempt
        vTaskDelay(pdMS_TO_TICKS(100));
        got = lastReadMs() != before;
      }
    }
  }
  dropClient(c);
  return got;
}

static void bmsTask(void*) {
  BLEDevice::init("");
  uint32_t next = millis() + BMS_INTERVAL_MS;
  for (;;) {
    while ((int32_t)(millis() - next) < 0) vTaskDelay(pdMS_TO_TICKS(1000));
    next += BMS_INTERVAL_MS;  // anchored to boot, a slow cycle doesn't shift the schedule
    bool ok = readOnce() && isFresh(lastReadMs());
    MESH_DEBUG_PRINTLN("BMS cycle: %s", ok ? "ok" : "failed");
    portENTER_CRITICAL(&bms_mux);
    cycle_ok = ok;
    cycle_done = true;
    portEXIT_CRITICAL(&bms_mux);
  }
}

bool BmsBleSensorManager::begin() {
  bool ok = EnvironmentSensorManager::begin();
  xTaskCreate(bmsTask, "bms_ble", 6144, NULL, 1, NULL);
  return ok;
}

bool BmsBleSensorManager::pollBmsCycle(bool& ok) {
  portENTER_CRITICAL(&bms_mux);
  bool done = cycle_done;
  ok = cycle_ok;
  cycle_done = false;
  portEXIT_CRITICAL(&bms_mux);
  return done;
}

static bool snapshot(bms_run_t& r) {
  portENTER_CRITICAL(&bms_mux);
  r = bms_last;
  uint32_t at = bms_last_ms;
  portEXIT_CRITICAL(&bms_mux);
  return isFresh(at);
}

bool BmsBleSensorManager::formatBmsSummary(char* dest, size_t len) {
  bms_run_t r;
  if (!snapshot(r)) return false;
  snprintf(dest, len, "BMS %.1fV %+.1fA SOC %.0f%% %.0fAh cel %.3f-%.3fV T %d/%dC ciclos %u",
           r.pack_v, r.current_a, r.soc, r.remain_ah, r.min_cell_mv / 1000.0f, r.max_cell_mv / 1000.0f,
           r.min_temp_c, r.max_temp_c, r.cycles);
  return true;
}

bool BmsBleSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  EnvironmentSensorManager::querySensors(requester_permissions, telemetry);
  if (!(requester_permissions & TELEM_PERM_ENVIRONMENT)) return true;

  bms_run_t r;
  if (!snapshot(r)) return true;  // nothing current in memory: no BMS fields

  // LPP current/power are unsigned, so direction goes in the digital input:
  // 0 idle, 1 charging, 2 discharging (register 47).
  uint8_t ch = next_available_channel++;
  telemetry.addVoltage(ch, r.pack_v);
  telemetry.addCurrent(ch, fabsf(r.current_a));
  telemetry.addPercentage(ch, (uint32_t)roundf(r.soc));
  telemetry.addPower(ch, r.power_w);
  telemetry.addTemperature(ch, r.max_temp_c);
  telemetry.addDigitalInput(ch, r.state);
  telemetry.addGenericSensor(ch, r.cycles);

  ch = next_available_channel++;
  telemetry.addVoltage(ch, r.max_cell_mv / 1000.0f);
  telemetry.addTemperature(ch, r.min_temp_c);

  ch = next_available_channel++;
  telemetry.addVoltage(ch, r.min_cell_mv / 1000.0f);

  for (int i = 0; i < r.cells && i < 32 && telemetry.getSize() + 4 <= BMS_TELEM_BUDGET; i++) {
    telemetry.addVoltage(next_available_channel++, r.cell_mv[i] / 1000.0f);
  }
  return true;
}
#endif
