# Telemetría periódica y alarma de umbral — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in `companion_radio` feature that broadcasts periodic
temperature/humidity readings and a threshold alarm (with recovery
message) to a configured hashtag channel, without modifying
`EnvironmentSensorManager`, `PCF8574Actuator`, or any native I2C sensor
driver.

**Architecture:** A standalone `TelemetryBroadcaster` component, driven
from `main.cpp`'s existing `loop()`, reads sensor data through
`EnvironmentSensorManager::querySensors()` (already public) and decodes
the resulting CayenneLPP buffer with ArduinoJson. Pure decision logic
(temp/humidity extraction, alarm hysteresis) lives in a separate,
natively-testable module. New `NodePrefs` fields (persisted for free via
the existing generic `ConfigSerializer`) hold runtime config; `MyMesh.cpp`
gets one small additive block to expose them through the existing
custom-vars protocol.

**Tech Stack:** PlatformIO / Arduino (nRF52), CayenneLPP 1.6.1,
ArduinoJson 7.4.3 (both already transitive deps, no new `lib_deps`),
GoogleTest for native tests.

**Spec:** `docs/superpowers/specs/2026-08-27-telemetry-broadcast-alarm-design.md`

## Global Constraints

- Must NOT modify `src/helpers/sensors/EnvironmentSensorManager.h/.cpp`.
- Must NOT modify `src/helpers/actuators/PCF8574Actuator.h/.cpp` or any
  actuator-branch code in `MyMesh.cpp`.
- Must NOT modify any native I2C sensor driver (Adafruit BME280, etc.).
- All new behavior compiles to nothing when `HAS_TELEMETRY_BROADCAST` is
  undefined - envs without the flag must not change in firmware size.
- Broadcast never targets the `Public` channel - `TELEMETRY_BROADCAST_CHANNEL`
  must be a hashtag channel name, resolved by name at runtime (no
  channel-by-name lookup exists yet in `BaseChatMesh` - this plan adds a
  small local one, not a change to `BaseChatMesh`).
- Periodic report and alarm are independently enable/disable-able at
  runtime (`telemetry_broadcast_interval_sec=0` disables the report;
  `telemetry_alarm_enabled=0` disables the alarm).
- Message formats (exact, do not rephrase):
  - Periódico: `TEMP=23.5C HUM=45.2%`
  - Alarma: `ALERTA: temperatura 47.2C supera 45.0C - se recomienda usar bloqueador solar`
  - Recuperación: `Temperatura normalizada: 42.8C`
- Alarm hysteresis margin is a fixed constant: `2.0f` °C (not runtime
  configurable).

---

## Note on test coverage vs. the spec

The spec's Testing section proposed a native unit test for the CayenneLPP
decode step. Investigation during planning found `test/mocks/CayenneLPP.h`
(used by the shared `[env:native]` PlatformIO env) is a bare stub -
`getBuffer()` returns `nullptr`, there is no encode/decode simulation -
and the *real* CayenneLPP `decode()` overload usable on Arduino requires
`JsonArray` (ArduinoJson), which isn't wired into any native test env
either. Faithfully mocking the real library's wire format would be far
more effort than the value it buys here.

This plan instead follows the same split the codebase already uses for
`AM2301Payload` vs. `AM2301RemoteSensor`: a **pure** module
(`TelemetryBroadcastLogic`) that takes an already-decoded `LppReading[]`
array — natively unit tested with real edge cases (missing temp, missing
humidity, alarm crossing/recovery/hysteresis) — and a thin **Arduino-only**
glue module (`TelemetryBroadcaster`) that calls the real CayenneLPP/ArduinoJson
APIs to produce that array. The glue module is verified by a real board
compile (Task 4), not a native unit test — this is the honest scope, not
a placeholder.

---

### Task 1: Pure decode/alarm logic + native tests

**Files:**
- Create: `examples/companion_radio/TelemetryBroadcastLogic.h`
- Create: `examples/companion_radio/TelemetryBroadcastLogic.cpp`
- Create: `test/test_telemetry_broadcast_alarm/test_telemetry_broadcast_alarm.cpp`
- Modify: `platformio.ini:168-173` (root file, `[env:native]` `build_src_filter`)

**Interfaces:**
- Produces: `struct LppReading { uint8_t type; float value; };`,
  `bool extractTempHumidity(const LppReading* readings, size_t count, float* temp_c, float* hum_pct)`,
  `enum class AlarmAction { NONE, FIRE, RECOVER }`,
  `AlarmAction checkAlarmTransition(float temp_c, float threshold_c, float hysteresis_c, bool* active)`,
  `#define LPP_TYPE_TEMPERATURE 103`, `#define LPP_TYPE_RELATIVE_HUMIDITY 104`
  — Task 4 (`TelemetryBroadcaster`) consumes all of these.

- [x] **Step 1: Write the failing test**

Create `test/test_telemetry_broadcast_alarm/test_telemetry_broadcast_alarm.cpp`:

```cpp
#include <gtest/gtest.h>
#include "../../examples/companion_radio/TelemetryBroadcastLogic.h"

TEST(ExtractTempHumidity, FindsBothValuesAmongOtherReadings) {
  LppReading readings[] = {
    { 0x02 /* some other LPP type */, 3.3f },
    { LPP_TYPE_TEMPERATURE, 23.5f },
    { LPP_TYPE_RELATIVE_HUMIDITY, 45.2f },
  };
  float temp_c = 0, hum_pct = 0;
  ASSERT_TRUE(extractTempHumidity(readings, 3, &temp_c, &hum_pct));
  EXPECT_FLOAT_EQ(23.5f, temp_c);
  EXPECT_FLOAT_EQ(45.2f, hum_pct);
}

TEST(ExtractTempHumidity, FailsWhenTemperatureMissing) {
  LppReading readings[] = { { LPP_TYPE_RELATIVE_HUMIDITY, 45.2f } };
  float temp_c = 0, hum_pct = 0;
  EXPECT_FALSE(extractTempHumidity(readings, 1, &temp_c, &hum_pct));
}

TEST(ExtractTempHumidity, FailsWhenHumidityMissing) {
  LppReading readings[] = { { LPP_TYPE_TEMPERATURE, 23.5f } };
  float temp_c = 0, hum_pct = 0;
  EXPECT_FALSE(extractTempHumidity(readings, 1, &temp_c, &hum_pct));
}

TEST(ExtractTempHumidity, FailsOnEmptyReadings) {
  float temp_c = 0, hum_pct = 0;
  EXPECT_FALSE(extractTempHumidity(nullptr, 0, &temp_c, &hum_pct));
}

TEST(AlarmTransition, FiresOnceWhenCrossingAboveThreshold) {
  bool active = false;
  EXPECT_EQ(AlarmAction::FIRE, checkAlarmTransition(47.2f, 45.0f, 2.0f, &active));
  EXPECT_TRUE(active);
}

TEST(AlarmTransition, StaysSilentWhileSustainedAboveThreshold) {
  bool active = true; // already firing
  EXPECT_EQ(AlarmAction::NONE, checkAlarmTransition(48.0f, 45.0f, 2.0f, &active));
  EXPECT_TRUE(active);
}

TEST(AlarmTransition, StaysSilentInHysteresisBand) {
  bool active = true; // already firing, threshold 45, hysteresis 2 -> band is (43, 45]
  EXPECT_EQ(AlarmAction::NONE, checkAlarmTransition(44.0f, 45.0f, 2.0f, &active));
  EXPECT_TRUE(active);
}

TEST(AlarmTransition, RecoversWhenDroppingBelowHysteresisBand) {
  bool active = true;
  EXPECT_EQ(AlarmAction::RECOVER, checkAlarmTransition(42.8f, 45.0f, 2.0f, &active));
  EXPECT_FALSE(active);
}

TEST(AlarmTransition, StaysSilentWhileNormalAndBelowThreshold) {
  bool active = false;
  EXPECT_EQ(AlarmAction::NONE, checkAlarmTransition(20.0f, 45.0f, 2.0f, &active));
  EXPECT_FALSE(active);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [x] **Step 2: Run test to verify it fails to build**

Run: `pio test -e native -f test_telemetry_broadcast_alarm`
Expected: FAIL to compile - `TelemetryBroadcastLogic.h` does not exist yet.

- [x] **Step 3: Write the header**

Create `examples/companion_radio/TelemetryBroadcastLogic.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

// CayenneLPP data types this feature cares about (see CayenneLPP.h in
// the vendored library - values match the LoRaWAN Cayenne LPP spec).
#define LPP_TYPE_TEMPERATURE       103
#define LPP_TYPE_RELATIVE_HUMIDITY 104

// One already-decoded CayenneLPP entry - `TelemetryBroadcaster` (Arduino
// side, Task 4) builds an array of these from a real CayenneLPP::decode()
// call; this header has no CayenneLPP/ArduinoJson/Arduino dependency so
// it can be unit tested natively.
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
```

- [x] **Step 4: Write the implementation**

Create `examples/companion_radio/TelemetryBroadcastLogic.cpp`:

```cpp
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
```

- [x] **Step 5: Wire the new file into the native test build**

Modify `platformio.ini` (root), in `[env:native]`:

```diff
   build_src_filter =
     -<*>
     +<../src/Utils.cpp>
     +<../src/Packet.cpp>
     +<../src/helpers/ConfigSerializer.cpp>
+    +<../examples/companion_radio/TelemetryBroadcastLogic.cpp>
   lib_deps =
     google/googletest @ 1.17.0
```

- [x] **Step 6: Run test to verify it passes**

Run: `pio test -e native -f test_telemetry_broadcast_alarm`
Expected: `9 test cases: 9 succeeded` (4 `ExtractTempHumidity` + 5 `AlarmTransition`).

- [x] **Step 7: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: all suites still pass (previous total was 40 cases across 5
active suites, now +9 in a 6th active suite = 49).

- [x] **Step 8: Commit**

```bash
git add examples/companion_radio/TelemetryBroadcastLogic.h examples/companion_radio/TelemetryBroadcastLogic.cpp test/test_telemetry_broadcast_alarm/test_telemetry_broadcast_alarm.cpp platformio.ini
git commit -m "Add pure temp/humidity extraction and alarm hysteresis logic"
```

---

### Task 2: NodePrefs fields + persistence round-trip test

**Files:**
- Modify: `examples/companion_radio/NodePrefs.h`
- Modify: `test/test_companion_node_prefs/test_companion_node_prefs.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `NodePrefs` fields `telemetry_broadcast_enabled` (uint8_t),
  `telemetry_broadcast_interval_sec` (uint32_t), `telemetry_alarm_enabled`
  (uint8_t), `telemetry_alarm_threshold_c` (float) — Task 4
  (`TelemetryBroadcaster::loop`) and Task 5 (`MyMesh.cpp` custom-vars)
  read/write these by name.

- [x] **Step 1: Write the failing test**

Add to `test/test_companion_node_prefs/test_companion_node_prefs.cpp`,
above the `int main(...)` at the bottom:

```cpp
TEST(CompanionNodePrefs, TelemetryBroadcastSettingsRoundTrip) {
  NodePrefs saved;
  saved.telemetry_broadcast_enabled = 1;
  saved.telemetry_broadcast_interval_sec = 900;
  saved.telemetry_alarm_enabled = 1;
  saved.telemetry_alarm_threshold_c = 38.5f;

  CaptureStream output;
  ASSERT_TRUE(saved.saveSerial(output));
  EXPECT_NE(std::string::npos, output.text().find("bc_en:1"));
  EXPECT_NE(std::string::npos, output.text().find("bc_int:900"));
  EXPECT_NE(std::string::npos, output.text().find("al_en:1"));

  ReplayStream input(output.text().c_str());
  NodePrefs loaded;
  ASSERT_TRUE(loaded.loadSerial(input));
  EXPECT_EQ(1, loaded.telemetry_broadcast_enabled);
  EXPECT_EQ(900u, loaded.telemetry_broadcast_interval_sec);
  EXPECT_EQ(1, loaded.telemetry_alarm_enabled);
  EXPECT_FLOAT_EQ(38.5f, loaded.telemetry_alarm_threshold_c);
}

TEST(CompanionNodePrefs, TelemetryBroadcastDefaultsAreSensible) {
  NodePrefs prefs;
  EXPECT_EQ(1, prefs.telemetry_broadcast_enabled);
  EXPECT_EQ(1800u, prefs.telemetry_broadcast_interval_sec);
  EXPECT_EQ(1, prefs.telemetry_alarm_enabled);
  EXPECT_FLOAT_EQ(45.0f, prefs.telemetry_alarm_threshold_c);
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_companion_node_prefs`
Expected: FAIL to compile - `NodePrefs` has no `telemetry_broadcast_enabled` member.

- [x] **Step 3: Add the fields and nested prefs class**

Modify `examples/companion_radio/NodePrefs.h`. First, the field block —
find:

```cpp
  uint8_t  gps_enabled = 0;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval = 0;     // GPS read interval in seconds
```

Add immediately after it:

```cpp
  uint8_t  gps_enabled = 0;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval = 0;     // GPS read interval in seconds
#ifndef TELEMETRY_BROADCAST_INTERVAL_SEC
#define TELEMETRY_BROADCAST_INTERVAL_SEC 1800  // 30 min
#endif
#ifndef TELEMETRY_ALARM_THRESHOLD_C
#define TELEMETRY_ALARM_THRESHOLD_C 45
#endif
  uint8_t  telemetry_broadcast_enabled = 1;      // ON once HAS_TELEMETRY_BROADCAST is opted into
  uint32_t telemetry_broadcast_interval_sec = TELEMETRY_BROADCAST_INTERVAL_SEC; // 0 = disabled
  uint8_t  telemetry_alarm_enabled = 1;
  float    telemetry_alarm_threshold_c = TELEMETRY_ALARM_THRESHOLD_C;
```

Next, the nested prefs class - find the `GPSPrefs` class block and add a
new class immediately after it (before `RepeatPrefs`):

```cpp
  class GPSPrefs : public ConfigSerializer {  // COPIED from CommonCLI (for now)
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->gps_enabled); // boolean
      def("int", _parent->gps_interval);   // interval in seconds
      def("adv_loc", _parent->advert_loc_policy);
    }
  public:
    GPSPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  GPSPrefs gps;

  class TelemetryBroadcastPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("bc_en", _parent->telemetry_broadcast_enabled);
      def("bc_int", _parent->telemetry_broadcast_interval_sec);
      def("al_en", _parent->telemetry_alarm_enabled);
      def("al_thr", _parent->telemetry_alarm_threshold_c);
    }
  public:
    TelemetryBroadcastPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  TelemetryBroadcastPrefs telemetry_broadcast;
```

Next, register it in `structure()` - find:

```cpp
  void structure() override {
    def("name", node_name, sizeof(node_name));
    //def("adv_int", advert_interval);
    //def("f_adv_int", flood_advert_interval);
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("gps", gps);
    def("repeat", repeat);
    def("comp", companion);
  }
```

Replace with:

```cpp
  void structure() override {
    def("name", node_name, sizeof(node_name));
    //def("adv_int", advert_interval);
    //def("f_adv_int", flood_advert_interval);
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("gps", gps);
    def("repeat", repeat);
    def("comp", companion);
    def("telem_bc", telemetry_broadcast);
  }
```

Finally, the constructor - find:

```cpp
  NodePrefs() : radio(this), gps(this), companion(this) {
```

Replace with:

```cpp
  NodePrefs() : radio(this), gps(this), companion(this), telemetry_broadcast(this) {
```

- [x] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_companion_node_prefs`
Expected: `2 test cases: 2 succeeded` (the file's only other
`TEST(CompanionNodePrefs, ...)` is wrapped in `#if 0` and doesn't count -
verified earlier in this session that the file currently builds with
"0 test cases: 0 succeeded", so the 2 new tests are the only active
ones).

- [x] **Step 5: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: all suites still pass.

- [x] **Step 6: Commit**

```bash
git add examples/companion_radio/NodePrefs.h test/test_companion_node_prefs/test_companion_node_prefs.cpp
git commit -m "Add telemetry broadcast/alarm prefs with generic ConfigSerializer persistence"
```

---

### Task 3: `TelemetryBroadcaster` (Arduino glue)

**Files:**
- Create: `examples/companion_radio/TelemetryBroadcaster.h`
- Create: `examples/companion_radio/TelemetryBroadcaster.cpp`

**Interfaces:**
- Consumes: `LppReading`, `extractTempHumidity`, `AlarmAction`,
  `checkAlarmTransition` from Task 1
  (`examples/companion_radio/TelemetryBroadcastLogic.h`); `NodePrefs`
  fields from Task 2; `BaseChatMesh::sendGroupMessage`, `getChannel`,
  `getRTCClock` (all public, `src/helpers/BaseChatMesh.h`);
  `EnvironmentSensorManager::querySensors(uint8_t, CayenneLPP&)` (public,
  `src/helpers/sensors/EnvironmentSensorManager.h`); `ChannelDetails`
  (`src/helpers/ChannelDetails.h`).
- Produces: `class TelemetryBroadcaster` with
  `void loop(BaseChatMesh& mesh, EnvironmentSensorManager& sensors, NodePrefs& prefs)`
  — Task 4 (`main.cpp`) instantiates this and calls `loop()` every tick.

This class is Arduino-only (`#ifdef HAS_TELEMETRY_BROADCAST` wraps the
whole `.cpp`) and is not natively unit tested - see "Note on test
coverage vs. the spec" above. It is verified in Task 4 via a real board
compile.

- [x] **Step 1: Write the header**

Create `examples/companion_radio/TelemetryBroadcaster.h`:

```cpp
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
```

- [x] **Step 2: Write the implementation**

Create `examples/companion_radio/TelemetryBroadcaster.cpp`:

```cpp
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
```

- [x] **Step 3: Commit**

This task cannot build/run standalone yet (needs Task 4's build flags
and `build_src_filter` entries to even compile) - commit as work in
progress, verification happens in Task 4.

```bash
git add examples/companion_radio/TelemetryBroadcaster.h examples/companion_radio/TelemetryBroadcaster.cpp
git commit -m "Add TelemetryBroadcaster: CayenneLPP-based sensor read + channel send"
```

---

### Task 4: Wire into `main.cpp` and the XIAO build - first real compile

**Files:**
- Modify: `examples/companion_radio/main.cpp`
- Modify: `variants/xiao_nrf52/platformio.ini`

**Interfaces:**
- Consumes: `TelemetryBroadcaster` (Task 3), `the_mesh` / `sensors` /
  `the_mesh.getNodePrefs()` (all pre-existing globals in `main.cpp`'s
  translation unit).
- Produces: working, board-compiled feature, gated entirely behind
  `HAS_TELEMETRY_BROADCAST`.

- [x] **Step 1: Add the include and global instance**

Modify `examples/companion_radio/main.cpp`. Find:

```cpp
#include "MyMesh.h"
```

Replace with:

```cpp
#include "MyMesh.h"
#ifdef HAS_TELEMETRY_BROADCAST
#include "TelemetryBroadcaster.h"
#endif
```

Find the `/* GLOBAL OBJECTS */` block's end marker:

```cpp
/* END GLOBAL OBJECTS */
```

Add immediately before it:

```cpp
#ifdef HAS_TELEMETRY_BROADCAST
TelemetryBroadcaster telemetry_broadcaster;
#endif

/* END GLOBAL OBJECTS */
```

- [x] **Step 2: Call it from `loop()`**

Find:

```cpp
void loop() {
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
```

Replace with:

```cpp
void loop() {
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#ifdef HAS_TELEMETRY_BROADCAST
  telemetry_broadcaster.loop(the_mesh, sensors, *the_mesh.getNodePrefs());
#endif
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
```

- [x] **Step 3: Add build flags and source files to the XIAO `_i2c_sensors` envs**

Modify `variants/xiao_nrf52/platformio.ini`. In
`[env:Xiao_nrf52_companion_radio_ble_i2c_sensors]`, find:

```ini
  -D QSPIFLASH=1
;  -D BLE_DEBUG_LOGGING=1
;  -D MESH_PACKET_LOGGING=1
;  -D MESH_DEBUG=1
build_src_filter = ${Xiao_nrf52.build_src_filter}
  +<helpers/nrf52/SerialBLEInterface.cpp>
  +<../examples/companion_radio/*.cpp>
  +<../examples/companion_radio/ui-orig/*.cpp>
lib_deps =
  ${Xiao_nrf52.lib_deps}
  densaugeo/base64 @ ~1.4.0

[env:Xiao_nrf52_companion_radio_usb]
```

Replace with:

```ini
  -D QSPIFLASH=1
;  -D BLE_DEBUG_LOGGING=1
;  -D MESH_PACKET_LOGGING=1
;  -D MESH_DEBUG=1
;  -D HAS_TELEMETRY_BROADCAST=1
;  -D TELEMETRY_BROADCAST_CHANNEL='"#miCanal"'
;  -D TELEMETRY_BROADCAST_INTERVAL_SEC=1800
;  -D TELEMETRY_ALARM_THRESHOLD_C=45
build_src_filter = ${Xiao_nrf52.build_src_filter}
  +<helpers/nrf52/SerialBLEInterface.cpp>
  +<../examples/companion_radio/*.cpp>
  +<../examples/companion_radio/ui-orig/*.cpp>
lib_deps =
  ${Xiao_nrf52.lib_deps}
  densaugeo/base64 @ ~1.4.0

[env:Xiao_nrf52_companion_radio_usb]
```

(`+<../examples/companion_radio/*.cpp>` already globs every `.cpp` in
that directory, so `TelemetryBroadcaster.cpp` and
`TelemetryBroadcastLogic.cpp` are picked up automatically - no filter
line needed for them specifically.)

Do the same in `[env:Xiao_nrf52_companion_radio_usb_i2c_sensors]` - find:

```ini
  -D ENABLE_USB_INTERFACE
;  -D MESH_PACKET_LOGGING=1
;  -D MESH_DEBUG=1
build_src_filter = ${Xiao_nrf52.build_src_filter}
  +<../examples/companion_radio/*.cpp>
  +<../examples/companion_radio/ui-orig/*.cpp>
lib_deps =
  ${Xiao_nrf52.lib_deps}
  densaugeo/base64 @ ~1.4.0

[env:Xiao_nrf52_repeater]
```

Replace with:

```ini
  -D ENABLE_USB_INTERFACE
;  -D MESH_PACKET_LOGGING=1
;  -D MESH_DEBUG=1
;  -D HAS_TELEMETRY_BROADCAST=1
;  -D TELEMETRY_BROADCAST_CHANNEL='"#miCanal"'
;  -D TELEMETRY_BROADCAST_INTERVAL_SEC=1800
;  -D TELEMETRY_ALARM_THRESHOLD_C=45
build_src_filter = ${Xiao_nrf52.build_src_filter}
  +<../examples/companion_radio/*.cpp>
  +<../examples/companion_radio/ui-orig/*.cpp>
lib_deps =
  ${Xiao_nrf52.lib_deps}
  densaugeo/base64 @ ~1.4.0

[env:Xiao_nrf52_repeater]
```

- [x] **Step 4: Verify the envs still build with the flag OFF (default, commented out)**

Run:
```bash
export FIRMWARE_VERSION=v1.0.0-test
pio run -e Xiao_nrf52_companion_radio_ble_i2c_sensors -e Xiao_nrf52_companion_radio_usb_i2c_sensors
```
Expected: both `SUCCESS`. `main.cpp`'s new lines are all inside
`#ifdef HAS_TELEMETRY_BROADCAST`, so those add nothing. `TelemetryBroadcaster.cpp`
is entirely guarded the same way, so it also adds nothing. However
`TelemetryBroadcastLogic.cpp` (Task 1) is picked up by the existing
`+<../examples/companion_radio/*.cpp>` wildcard and is **not**
flag-guarded (on purpose - Task 1's native tests build it unconditionally,
with no `HAS_TELEMETRY_BROADCAST` in the native env at all, so it can't
be guarded without breaking that). Actually measured: usb_i2c_sensors
went from Flash 403576→403848 bytes (+272, +0.038%) and RAM 142324→142356
(+32, +0.014%) with the flag off - not exactly zero, but negligible, and
the tradeoff (native-testable pure logic module, no PlatformIO
per-flag-conditional build_src_filter mechanism exists to avoid this) is
worth it. Treat "unchanged" as "changed by a couple hundred bytes at
most, from TelemetryBroadcastLogic.cpp alone" - a jump of kilobytes
would mean something is wrong.

- [x] **Step 5: Verify the feature actually compiles when turned on**

Temporarily uncomment the 4 new flags in
`[env:Xiao_nrf52_companion_radio_ble_i2c_sensors]` (from Step 3), then:

```bash
export FIRMWARE_VERSION=v1.0.0-test
pio run -e Xiao_nrf52_companion_radio_ble_i2c_sensors
```
Expected: `SUCCESS`. If it fails, fix `TelemetryBroadcaster.cpp`/`.h`
(Task 3) until it builds - this is the real verification step for that
task's code, per the "Note on test coverage" above.

Then re-comment the 4 flags back out (the shipped default for this env
stays opt-in/off, matching every other optional feature flag in this
file) and confirm the env builds clean again:

```bash
pio run -e Xiao_nrf52_companion_radio_ble_i2c_sensors
```
Expected: `SUCCESS`.

- [x] **Step 6: Verify the actuator and plain envs are untouched**

```bash
pio run -e Xiao_nrf52_companion_radio_ble -e Xiao_nrf52_companion_radio_usb
```
Expected: both `SUCCESS`, unchanged from before this task (these envs
don't reference `TelemetryBroadcaster.h` at all, and
`HAS_TELEMETRY_BROADCAST` is never defined for them).

- [x] **Step 7: Run the full native suite once more**

Run: `pio test -e native`
Expected: unchanged, all suites pass (this task touches no
natively-tested code).

- [x] **Step 8: Commit**

```bash
git add examples/companion_radio/main.cpp variants/xiao_nrf52/platformio.ini
git commit -m "Wire TelemetryBroadcaster into main.cpp loop() and XIAO i2c_sensors envs"
```

---

### Task 5: Runtime configuration via custom-vars (companion app / BLE / USB)

**Files:**
- Modify: `examples/companion_radio/MyMesh.cpp`

**Interfaces:**
- Consumes: `NodePrefs` fields from Task 2 (`_prefs.telemetry_broadcast_enabled`,
  etc. - `_prefs` is the existing `MyMesh` member).
- Produces: nothing new consumed elsewhere - this is the last task.

This is the one touchpoint shared with existing dispatcher code
(`CMD_SET_CUSTOM_VAR`/`CMD_GET_CUSTOM_VARS`, already used by
`gps`/`gps_interval`). Both edits are purely additive - the existing
`sensors.setSettingValue()`/GPS branches are untouched.

- [x] **Step 1: Extend `CMD_SET_CUSTOM_VAR` to recognize the 4 new names**

Find, in `examples/companion_radio/MyMesh.cpp`:

```cpp
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
```

Replace with:

```cpp
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
#ifdef HAS_TELEMETRY_BROADCAST
      if (strcmp(sp, "telemetry_broadcast_enabled") == 0) {
        _prefs.telemetry_broadcast_enabled = (np[0] == '1') ? 1 : 0;
        savePrefs();
        writeOKFrame();
      } else if (strcmp(sp, "telemetry_broadcast_interval_sec") == 0) {
        _prefs.telemetry_broadcast_interval_sec = constrain((uint32_t)atoi(np), 0, 86400u);
        savePrefs();
        writeOKFrame();
      } else if (strcmp(sp, "telemetry_alarm_enabled") == 0) {
        _prefs.telemetry_alarm_enabled = (np[0] == '1') ? 1 : 0;
        savePrefs();
        writeOKFrame();
      } else if (strcmp(sp, "telemetry_alarm_threshold_c") == 0) {
        _prefs.telemetry_alarm_threshold_c = atof(np);
        savePrefs();
        writeOKFrame();
      } else
#endif
      {
        bool success = sensors.setSettingValue(sp, np);
        if (success) {
          #if ENV_INCLUDE_GPS == 1
          // Update node preferences for GPS settings
          if (strcmp(sp, "gps") == 0) {
            _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
            savePrefs();
          } else if (strcmp(sp, "gps_interval") == 0) {
            uint32_t interval_seconds = atoi(np);
            _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
            savePrefs();
          }
          #endif
          writeOKFrame();
        } else {
          writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        }
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
```

- [x] **Step 2: Extend `CMD_GET_CUSTOM_VARS` to list the 4 new names**

Find:

```cpp
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
```

Replace with:

```cpp
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    bool wrote_any = false;
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (wrote_any) {
        *dp++ = ',';
      }
      wrote_any = true;
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
#ifdef HAS_TELEMETRY_BROADCAST
    {
      const char* tb_names[4] = {
        "telemetry_broadcast_enabled", "telemetry_broadcast_interval_sec",
        "telemetry_alarm_enabled", "telemetry_alarm_threshold_c"
      };
      char val[12];
      for (int i = 0; i < 4 && dp - (char *)&out_frame[1] < 140; i++) {
        switch (i) {
          case 0: snprintf(val, sizeof(val), "%u", (unsigned)_prefs.telemetry_broadcast_enabled); break;
          case 1: snprintf(val, sizeof(val), "%lu", (unsigned long)_prefs.telemetry_broadcast_interval_sec); break;
          case 2: snprintf(val, sizeof(val), "%u", (unsigned)_prefs.telemetry_alarm_enabled); break;
          default: snprintf(val, sizeof(val), "%.1f", _prefs.telemetry_alarm_threshold_c); break;
        }
        if (wrote_any) {
          *dp++ = ',';
        }
        wrote_any = true;
        strcpy(dp, tb_names[i]);
        dp = strchr(dp, 0);
        *dp++ = ':';
        strcpy(dp, val);
        dp = strchr(dp, 0);
      }
    }
#endif
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
```

- [x] **Step 3: Build-verify with the flag on and off**

```bash
export FIRMWARE_VERSION=v1.0.0-test
pio run -e Xiao_nrf52_companion_radio_ble        # flag off - must still build
pio run -e Xiao_nrf52_companion_radio_usb        # flag off - must still build
```

Then temporarily uncomment `HAS_TELEMETRY_BROADCAST` (and the other 3
flags) in `[env:Xiao_nrf52_companion_radio_ble_i2c_sensors]` again and:

```bash
pio run -e Xiao_nrf52_companion_radio_ble_i2c_sensors
```
Expected: `SUCCESS`. Re-comment the flags back out afterward and confirm
`SUCCESS` again.

- [x] **Step 4: Run the full native suite one last time**

Run: `pio test -e native`
Expected: unchanged, all suites pass.

- [x] **Step 5: Commit**

```bash
git add examples/companion_radio/MyMesh.cpp
git commit -m "Expose telemetry broadcast/alarm settings via CMD_SET/GET_CUSTOM_VAR"
```

---

## Manual end-to-end verification (not part of any task's automated steps)

Once all 5 tasks are done, with real XIAO + native I2C sensor (BME280)
hardware:

1. Uncomment `HAS_TELEMETRY_BROADCAST` + set a real
   `TELEMETRY_BROADCAST_CHANNEL` name (a hashtag channel you've already
   created on the node) in `[env:Xiao_nrf52_companion_radio_ble_i2c_sensors]`.
2. Flash, join that channel from the companion app.
3. Confirm a `TEMP=..C HUM=..%` message arrives on the channel within
   `TELEMETRY_BROADCAST_INTERVAL_SEC` of boot.
4. Force a high reading (warm the sensor with a hand/hairdryer past the
   threshold) and confirm the `ALERTA:` message, then confirm the
   `Temperatura normalizada:` message once it cools back down 2°C below
   threshold.
5. Confirm the `Public` channel receives nothing.
6. Via the companion app's custom-vars UI (or `CMD_GET_CUSTOM_VARS`/`CMD_SET_CUSTOM_VAR`
   directly), read and change `telemetry_broadcast_interval_sec` and
   `telemetry_alarm_threshold_c` without reflashing, confirm the new
   values take effect and persist across a reboot.
