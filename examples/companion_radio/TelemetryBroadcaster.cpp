#ifdef HAS_TELEMETRY_BROADCAST

#include "TelemetryBroadcaster.h"
#include "TelemetryBroadcastLogic.h"
#include <MeshCore.h>
#include <CayenneLPP.h>
#include <ArduinoJson.h>

bool TelemetryBroadcaster::findConfiguredChannel(BaseChatMesh& mesh, ChannelDetails& out) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (mesh.getChannel(i, out) && strcmp(out.name, TELEMETRY_BROADCAST_CHANNEL) == 0) {
      return true;
    }
  }
  return false;
}

bool TelemetryBroadcaster::readTempHumidity(EnvironmentSensorManager& sensors, float* temp_c, float* hum_pct) {
  CayenneLPP lpp(32);
  lpp.reset();
  sensors.querySensors(0xFF, lpp);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  if (lpp.decode(lpp.getBuffer(), lpp.getSize(), arr) == 0) {
    MESH_DEBUG_PRINTLN("TelemetryBroadcaster: CayenneLPP decode failed or empty");
    return false;
  }

  LppReading readings[8];
  size_t count = 0;
  for (JsonObject obj : arr) {
    if (count >= 8) break;
    readings[count].type = obj["type"].as<uint8_t>();
    readings[count].value = obj["value"].as<float>();
    count++;
  }

  return extractTempHumidity(readings, count, temp_c, hum_pct);
}

void TelemetryBroadcaster::sendReading(BaseChatMesh& mesh, NodePrefs& prefs, const ChannelDetails& ch, float temp_c, float hum_pct) {
  char msg[48];
  snprintf(msg, sizeof(msg), "TEMP=%.1fC HUM=%.1f%%", temp_c, hum_pct);
  mesh::GroupChannel channel = ch.channel;
  mesh.sendGroupMessage(mesh.getRTCClock()->getCurrentTimeUnique(), channel, prefs.node_name, msg, strlen(msg));
}

void TelemetryBroadcaster::checkAlarm(BaseChatMesh& mesh, NodePrefs& prefs, const ChannelDetails& ch, float temp_c) {
  AlarmAction action = checkAlarmTransition(temp_c, prefs.telemetry_alarm_threshold_c, ALARM_HYSTERESIS_C, &_alarm_active);
  if (action == AlarmAction::NONE) return;

  char msg[80];
  mesh::GroupChannel channel = ch.channel;
  if (action == AlarmAction::FIRE) {
    snprintf(msg, sizeof(msg), "ALERTA: temperatura %.1fC supera %.1fC - se recomienda usar bloqueador solar",
             temp_c, prefs.telemetry_alarm_threshold_c);
  } else {
    snprintf(msg, sizeof(msg), "Temperatura normalizada: %.1fC", temp_c);
  }
  mesh.sendGroupMessage(mesh.getRTCClock()->getCurrentTimeUnique(), channel, prefs.node_name, msg, strlen(msg));
}

void TelemetryBroadcaster::loop(BaseChatMesh& mesh, EnvironmentSensorManager& sensors, NodePrefs& prefs) {
  if (!prefs.telemetry_broadcast_enabled && !prefs.telemetry_alarm_enabled) return;

  ChannelDetails ch;
  if (!findConfiguredChannel(mesh, ch)) return; // channel not created yet - retry next loop()

  bool need_broadcast = prefs.telemetry_broadcast_enabled
    && prefs.telemetry_broadcast_interval_sec > 0
    && (millis() - _last_broadcast_ms >= prefs.telemetry_broadcast_interval_sec * 1000UL);
  bool need_alarm_check = prefs.telemetry_alarm_enabled;

  if (!need_broadcast && !need_alarm_check) return;

  float temp_c, hum_pct;
  if (!readTempHumidity(sensors, &temp_c, &hum_pct)) return; // no sensor reading yet - retry next loop()

  if (need_broadcast) {
    _last_broadcast_ms = millis();
    sendReading(mesh, prefs, ch, temp_c, hum_pct);
  }
  if (need_alarm_check) {
    checkAlarm(mesh, prefs, ch, temp_c);
  }
}

#endif // ifdef HAS_TELEMETRY_BROADCAST
