#pragma once

#ifdef HAS_AM2301_SENSOR

#include <Arduino.h>
#include <Wire.h>
#include "AM2301Payload.h"

#ifndef AM2301_SENSOR_I2C_ADDR
#define AM2301_SENSOR_I2C_ADDR 0x21
#endif

/*
 * Driver for the AM2301 Nano I2C bridge (see tools/i2c_sensor_am2301_nano
 * and README_I2C_SENSOR.md). The Nano reads the AM2301 locally and caches
 * the last known-good reading; this class just does the I2C read and
 * decodes the 5-byte payload - see AM2301Payload.h for the wire format.
 */
class AM2301RemoteSensor {
public:
  void begin(TwoWire& wire, uint8_t i2c_addr = AM2301_SENSOR_I2C_ADDR);

  // Returns false only on I2C bus error (no ack / wrong byte count) - in
  // that case *temp_c/*hum_pct/*status are left untouched. On true,
  // *status reports the Nano's own cache status (AM2301_STATUS_*), which
  // is a separate concern from the I2C bus transaction itself succeeding.
  bool read(float* temp_c, float* hum_pct, uint8_t* status);

private:
  TwoWire* _wire = NULL;
  uint8_t _addr = AM2301_SENSOR_I2C_ADDR;
};

#endif // ifdef HAS_AM2301_SENSOR
