#include "TelemetryBroadcastLogic.h"

bool extractTempHumidity(const LppReading* readings, size_t count, float* temp_c, float* hum_pct) {
  bool found_temp = false;
  bool found_hum = false;
  for (size_t i = 0; i < count; i++) {
    if (readings[i].type == LPP_TYPE_TEMPERATURE) {
      *temp_c = readings[i].value;
      found_temp = true;
    } else if (readings[i].type == LPP_TYPE_RELATIVE_HUMIDITY) {
      *hum_pct = readings[i].value;
      found_hum = true;
    }
  }
  return found_temp && found_hum;
}

AlarmAction checkAlarmTransition(float temp_c, float threshold_c, float hysteresis_c, bool* active) {
  if (!*active && temp_c > threshold_c) {
    *active = true;
    return AlarmAction::FIRE;
  }
  if (*active && temp_c < (threshold_c - hysteresis_c)) {
    *active = false;
    return AlarmAction::RECOVER;
  }
  return AlarmAction::NONE;
}
