#pragma once

#include <cstddef>
#include <cstdint>

// CayenneLPP data types this feature cares about (see CayenneLPP.h in
// the vendored library - values match the LoRaWAN Cayenne LPP spec).
#define LPP_TYPE_TEMPERATURE       103
#define LPP_TYPE_RELATIVE_HUMIDITY 104

// One already-decoded CayenneLPP entry - `TelemetryBroadcaster` (Arduino
// side) builds an array of these from a real CayenneLPP::decode() call;
// this header has no CayenneLPP/ArduinoJson/Arduino dependency so it can
// be unit tested natively.
struct LppReading {
  uint8_t type;
  float value;
};

// Scans `readings` for a temperature (type 103) and humidity (type 104)
// entry. Returns false if either is missing - the caller decides what
// "no reading" means for its own message (see TelemetryBroadcaster).
bool extractTempHumidity(const LppReading* readings, size_t count, float* temp_c, float* hum_pct);

enum class AlarmAction { NONE, FIRE, RECOVER };

// Pure alarm hysteresis state machine. `active` is the caller's
// persisted state (in/out): true while the alarm is currently firing.
// FIRE: temp just crossed above threshold_c (active was false).
// RECOVER: temp just dropped below (threshold_c - hysteresis_c) (active was true).
// NONE: no transition (includes staying in the hysteresis band while active).
AlarmAction checkAlarmTransition(float temp_c, float threshold_c, float hysteresis_c, bool* active);
