#pragma once

#include <stdint.h>

// Status byte values shared by the AM2301 Nano bridge (encoder, see
// tools/i2c_sensor_am2301_nano) and AM2301RemoteSensor (decoder, see
// AM2301RemoteSensor.h) - see README_I2C_SENSOR.md for the full wire
// format.
#define AM2301_STATUS_OK             0
#define AM2301_STATUS_CACHED         1
#define AM2301_STATUS_NO_READING_YET 2

// Decodes the 5-byte AM2301 sensor payload:
//   byte 0:   status (AM2301_STATUS_*)
//   byte 1-2: temperature * 10, int16_t, big-endian (allows negative)
//   byte 3-4: humidity * 10, uint16_t, big-endian
// Pure function, no I2C/Arduino dependency - buf must point to exactly
// 5 bytes.
void decodeAM2301Payload(const uint8_t* buf, float* temp_c, float* hum_pct, uint8_t* status);
