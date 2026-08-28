#pragma once

#ifdef HAS_TELEMETRY_BROADCAST

#include <helpers/BaseChatMesh.h>
#include <helpers/ChannelDetails.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include "NodePrefs.h"

#ifndef TELEMETRY_BROADCAST_CHANNEL
#error "HAS_TELEMETRY_BROADCAST requires TELEMETRY_BROADCAST_CHANNEL to be defined (a hashtag channel name, e.g. '\"#miCanal\"')"
#endif

class TelemetryBroadcaster {
public:
  void loop(BaseChatMesh& mesh, EnvironmentSensorManager& sensors, NodePrefs& prefs);

private:
  static constexpr float ALARM_HYSTERESIS_C = 2.0f;

  bool findConfiguredChannel(BaseChatMesh& mesh, ChannelDetails& out);
  bool readTempHumidity(EnvironmentSensorManager& sensors, float* temp_c, float* hum_pct);
  void sendReading(BaseChatMesh& mesh, NodePrefs& prefs, const ChannelDetails& ch, float temp_c, float hum_pct);
  void checkAlarm(BaseChatMesh& mesh, NodePrefs& prefs, const ChannelDetails& ch, float temp_c);

  unsigned long _last_broadcast_ms = 0;
  bool _alarm_active = false;
};

#endif // ifdef HAS_TELEMETRY_BROADCAST
