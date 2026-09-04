# Heltec Wireless Tracker BMP280 Telemetry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `heltec_tracker` (Heltec Wireless Tracker, v1) board respond to a companion-app "self telemetry" request with a BMP280's temperature, barometric pressure, and altitude, alongside its existing GPS location, over MeshCore's standard binary CayenneLPP telemetry mechanism.

**Architecture:** `heltec_tracker` currently uses a board-specific `HWTSensorManager` subclass (in its own `target.h`/`target.cpp`) that hand-rolls GPS handling and nothing else. `src/helpers/sensors/EnvironmentSensorManager` is the generic, already-shipped `SensorManager` implementation used elsewhere in this repo (e.g. `heltec_tracker_v2`, `xiao_nrf52` i2c_sensors envs) that: (1) takes the same `LocationProvider&` constructor `HWTSensorManager` does, so it is a drop-in replacement for the GPS path, and (2) scans the I2C bus at boot and auto-initializes any sensor from its built-in descriptor table whose address ACKs — including a `BMP280` entry gated behind the `ENV_INCLUDE_BMP280` build flag. Swapping `HWTSensorManager` for `EnvironmentSensorManager` and setting that flag gets BMP280 telemetry "for free," with zero changes needed in `examples/companion_radio/MyMesh.cpp` (its telemetry request handler already calls the generic `sensors.querySensors(permissions, telemetry)` — see `MyMesh.cpp:728` and `:1734`).

**Tech Stack:** PlatformIO / Arduino-ESP32, RadioLib, `Adafruit_BMP280` driver (via `Adafruit BMP280 Library`), MeshCore's `CayenneLPP` telemetry encoding.

**Spec:** No separate spec doc — this plan was scoped directly from a conversation with the user confirming: (a) the "standard telemetry" path over a bespoke channel-text command, (b) target board is `heltec_tracker` (not `heltec_tracker_v2`), (c) only `ENV_INCLUDE_BMP280=1` is being added — no other `ENV_INCLUDE_*` sensors.

## Global Constraints

- Only the `heltec_tracker` variant is touched. Do not modify `heltec_tracker_v2`, `EnvironmentSensorManager.cpp`, or `MyMesh.cpp` — they already support this generically.
- Add exactly one new build flag: `-D ENV_INCLUDE_BMP280=1`. Do not pull in the full `[sensor_base]` bundle (that would compile in ~15 unrelated sensor drivers no `heltec_tracker` unit will ever see).
- Pin the new lib_dep to the same version already used elsewhere in this repo: `adafruit/Adafruit BMP280 Library @ ^2.6.8` (see `platformio.ini:148`).
- Per stored user preference: do **not** run a full ESP32 board-env compile (`pio run -e <env>`) as a verification step in this plan — write and self-review the code, then hand off building/flashing/testing to the user.

---

### Task 1: Create the feature branch

**Files:** none (git operation only)

- [ ] **Step 1: Confirm working tree is clean and on `main`**

```bash
git status
git checkout main
git pull origin main
```

Expected: clean tree, `main` up to date with `origin/main` (the `120m4n/MeshCore-HJ7RMN` fork).

- [ ] **Step 2: Create and switch to the feature branch**

```bash
git checkout -b feature/heltec-tracker-bmp280-telemetry
```

Expected: `git branch --show-current` prints `feature/heltec-tracker-bmp280-telemetry`.

---

### Task 2: Swap `heltec_tracker`'s sensor manager to `EnvironmentSensorManager` and enable BMP280

**Files:**
- Modify: `variants/heltec_tracker/target.h`
- Modify: `variants/heltec_tracker/target.cpp`
- Modify: `variants/heltec_tracker/platformio.ini`

**Interfaces:**
- Consumes: `EnvironmentSensorManager` (from `src/helpers/sensors/EnvironmentSensorManager.h`) — constructor `EnvironmentSensorManager(LocationProvider &location)`, and the virtual `SensorManager` interface (`begin()`, `querySensors(uint8_t, CayenneLPP&)`, `loop()`, `getNumSettings()`, `getSettingName()`, `getSettingValue()`, `setSettingValue()`) it already implements — none of these are being modified in this task.
- Produces: the global `extern EnvironmentSensorManager sensors;` that `examples/companion_radio/MyMesh.cpp` already calls generically via `sensors.querySensors(...)` — no consumer-side changes needed.

- [ ] **Step 1: Replace `variants/heltec_tracker/target.h`**

Current file declares a bespoke `HWTSensorManager : public SensorManager` class that only forwards GPS. Delete that class entirely and swap the `sensors` global's type to `EnvironmentSensorManager`:

```cpp
#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <../heltec_v3/HeltecV3Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7735Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern HeltecV3Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
```

This mirrors `variants/heltec_tracker_v2/target.h` exactly (that variant already made this same swap).

- [ ] **Step 2: Replace `variants/heltec_tracker/target.cpp`**

Delete the `HWTSensorManager::start_gps/stop_gps/begin/querySensors/loop/getNumSettings/getSettingName/getSettingValue/setSettingValue` method bodies (lines 45-110 of the current file) — `EnvironmentSensorManager.cpp` already implements all of this generically (GPS start/stop via the `"gps"` setting, `loop()`, BMP280 scan/init/query). Replace the whole file with:

```cpp
#include <Arduino.h>
#include "target.h"

#include <helpers/sensors/MicroNMEALocationProvider.h>

HeltecV3Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
// GPS_EN (GPIO35) drives N-ch MOSFET → P-ch high-side switch; GPS_RESET (GPIO36) active LOW
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock, GPS_RESET, GPS_EN, &board.periph_power);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display(&board.periph_power);   // peripheral power pin is shared
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif

}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
```

Note: `EnvironmentSensorManager` uses the board's primary I2C bus (`Wire`, on `PIN_BOARD_SDA=45`/`PIN_BOARD_SCL=46` — already brought up by `ESP32Board::begin()`) since `ENV_PIN_SDA`/`ENV_PIN_SCL` (the flags that would switch it to a second bus, `Wire1`) are not defined for this board. The BMP280 (I2C address `0x76`) shares that bus without conflict.

- [ ] **Step 3: Edit `variants/heltec_tracker/platformio.ini` — add the BMP280 build flag**

In the `[Heltec_tracker_base]` section, add `-D ENV_INCLUDE_BMP280=1` right after the existing `-D ENV_INCLUDE_GPS=1` line (currently line 40):

```ini
  -D ENV_INCLUDE_GPS=1
  -D ENV_INCLUDE_BMP280=1
```

- [ ] **Step 4: Edit `variants/heltec_tracker/platformio.ini` — compile `EnvironmentSensorManager.cpp`**

`EnvironmentSensorManager.cpp` is a separate translation unit under `src/helpers/sensors/` that the current `build_src_filter` does not pull in (the repo's default `arduino_base.build_src_filter` only globs `helpers/*.cpp`, not the `helpers/sensors/` subdirectory). Add it explicitly to the `[Heltec_tracker_base]` section's `build_src_filter` (currently lines 45-46):

```ini
build_src_filter = ${esp32_base.build_src_filter}
  +<../variants/heltec_tracker>
  +<helpers/sensors/EnvironmentSensorManager.cpp>
```

(`MicroNMEALocationProvider.h` needs no entry — it's header-only, already compiling fine today via its `#include` in `target.cpp`.)

- [ ] **Step 5: Edit `variants/heltec_tracker/platformio.ini` — add the BMP280 driver dependency**

In the same `[Heltec_tracker_base]` section's `lib_deps` (currently lines 47-50), add the driver, pinned to the same version the rest of the repo uses:

```ini
lib_deps =
  ${esp32_base.lib_deps}
  stevemarple/MicroNMEA @ ^2.0.6
  bodmer/TFT_eSPI @ ^2.4.31
  adafruit/Adafruit BMP280 Library @ ^2.6.8
```

- [ ] **Step 6: Self-review the diff**

```bash
git diff variants/heltec_tracker/
```

Confirm: `target.h` no longer declares `HWTSensorManager`; `target.cpp` no longer defines any `HWTSensorManager::` methods and constructs `EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);`; `platformio.ini` has the new build flag, the new `build_src_filter` entry, and the new `lib_deps` entry, and nothing else in the file changed (the four `[env:Heltec_Wireless_Tracker_*]` sections below `[Heltec_tracker_base]` all inherit these changes via `${Heltec_tracker_base.*}` and need no direct edits).

- [ ] **Step 7: Commit**

```bash
git add variants/heltec_tracker/target.h variants/heltec_tracker/target.cpp variants/heltec_tracker/platformio.ini
git commit -m "Add BMP280 telemetry to heltec_tracker via EnvironmentSensorManager"
```

---

## Verification (hand-off, not automated in this plan)

Per stored preference, this plan does **not** run `pio run -e <env>` as a task step. Before flashing:

```bash
pio run -e Heltec_Wireless_Tracker_companion_radio_ble
```

(or `_usb`, `_repeater`, `_room_server`, `_kiss_modem` — all four inherit the same `[Heltec_tracker_base]` change) — run this yourself to confirm the build is clean, then flash and query telemetry from the companion app to confirm the BMP280's temperature/pressure/altitude come back alongside GPS. If no BMP280 is wired to `PIN_BOARD_SDA`/`PIN_BOARD_SCL` (GPIO 45/46) on the test unit, the boot-time I2C scan will simply skip it (`EnvironmentSensorManager` never touches an address that didn't ACK during the scan) — GPS-only telemetry will keep working exactly as before.
