#pragma once

#include <helpers/sensors/EnvironmentSensorManager.h>

// Adds a "SMART BMS" battery pack (BLE, Modbus-RTU slave 0xD2) to the normal environment
// sensors. Once per BMS_INTERVAL_MS (counted from boot) a background FreeRTOS task wakes,
// connects, reads one frame into memory and disconnects, so the mesh loop never blocks.
class BmsBleSensorManager : public EnvironmentSensorManager {
public:
  using EnvironmentSensorManager::EnvironmentSensorManager;
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;

  // True once per finished hourly cycle; ok = a fresh reading made it into memory.
  bool pollBmsCycle(bool& ok);
  // One-line summary of the reading in memory, for the public channel. False if none/stale.
  bool formatBmsSummary(char* dest, size_t len);
};
