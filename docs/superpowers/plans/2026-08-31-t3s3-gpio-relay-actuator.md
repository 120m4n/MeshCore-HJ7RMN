# T3S3 GPIO Relay Actuator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 4-channel relay actuator to the LilyGo T3S3 (SX1276) companion BLE firmware, controlled directly via GPIO and driven by `PIN<n>_ON`/`PIN<n>_OFF`/`PIN_STATUS` text commands sent on an authorized "hashtag" mesh channel.

**Architecture:** A pure, natively-testable logic module (`GPIORelayLogic`) implements command parsing, state-bit rendering, and active-low polarity translation with zero Arduino dependency. A thin Arduino driver (`GPIORelayActuator`) wraps that logic with `pinMode`/`digitalWrite` calls against 4 fixed GPIOs. `MyMesh` (companion radio firmware) wires channel messages into the driver, reusing the existing hashtag-channel authorization model. `LilygoT3S3SX1276Board::begin()` drives all 4 relay pins to a safe OFF state as the very first action in `setup()`, before anything else initializes.

**Tech Stack:** PlatformIO, Arduino framework (ESP32-S3), C++17, GoogleTest (native unit tests for the pure logic module).

**Spec:** `docs/superpowers/specs/2026-08-31-t3s3-gpio-relay-actuator-design.md`

## Global Constraints

- Target env: `LilyGo_T3S3_sx1276_companion_radio_ble` only (`variants/lilygo_t3s3_sx1276/platformio.ini`). No other env is touched.
- 4 channels, indexed 0-3 (`RELAY_MAX_PIN = 3`).
- Relay GPIOs: `RELAY_PIN0=16`, `RELAY_PIN1=15`, `RELAY_PIN2=39`, `RELAY_PIN3=40`. Do not use GPIO18 (conflicts with `PIN_BOARD_SDA`, the onboard OLED/RTC I2C bus already active in this env).
- Polarity: `RELAY_ACTIVE_LOW=1` defined by default (GPIO LOW = relay energized). All application-level code speaks logical ON/OFF only; the LOW/HIGH translation is isolated to one function.
- Command format: `<ACTUATOR_CMD_PREFIX><digit 0-3><ACTUATOR_CMD_ON_SUFFIX|ACTUATOR_CMD_OFF_SUFFIX>`, default prefix `"PIN"`, suffixes `"_ON"`/`"_OFF"`; status query `ACTUATOR_CMD_STATUS`, default `"PIN_STATUS"`. Matched as a **suffix** of the incoming text (group messages arrive as `"<sender name>: <text>"`).
- Only channels whose secret equals `sha256(channel_name)[:16]` ("hashtag channels") may trigger the actuator — excludes `Public` and any private/randomly-keyed channel.
- `ACTUATOR_SEND_ACK` stays opt-in (build flag presence), commented out by default — matches the existing repo convention of not spending LoRa airtime automatically.
- `PIN_STATUS` always reports the firmware's logical cache directly (no `(cached)`/`(resynced)` annotations) — there is no external chip that can diverge from firmware memory, unlike the I2C actuator design this pattern is adapted from.
- No I2C/Wire dependency anywhere in this feature.
- Do not modify any file belonging to the unrelated `feature/i2c-actuator-public-channel` branch's scope (`TelemetryBroadcaster`, `PCF8574Actuator`, etc.) — this branch was created fresh from `main` and must stay scoped to the GPIO relay feature only.

---

### Task 1: Pure relay logic — polarity translation + state-bit rendering (TDD)

**Files:**
- Create: `src/helpers/actuators/GPIORelayLogic.h`
- Create: `src/helpers/actuators/GPIORelayLogic.cpp`
- Create: `test/test_gpio_relay_logic/test_gpio_relay_logic.cpp`
- Modify: `platformio.ini:168-172` (`[env:native]` `build_src_filter`)

**Interfaces:**
- Produces: `#define RELAY_MAX_PIN 3` (in `GPIORelayLogic.h`); `bool relayLogicalToPhysicalHigh(bool logical_on, bool active_low)`; `void buildRelayStateBits(uint8_t state, char out[5])`. Both consumed later by `GPIORelayActuator` (Task 3) and `MyMesh::checkActuatorCommand` (Task 5).

- [ ] **Step 1: Create the header with declarations for this task's two functions**

```cpp
// src/helpers/actuators/GPIORelayLogic.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// 4-channel relay actuator, channels indexed 0-3. No Arduino/Wire
// dependency - unit tested natively (see test/test_gpio_relay_logic/).
#define RELAY_MAX_PIN 3

// Translates a logical ON/OFF request to the physical level that should be
// driven on the GPIO. Most cheap relay modules (SRD-05VDC-SL-C based) are
// active-low: GPIO LOW energizes the relay. `active_low` selects that
// behavior; returns true if the pin should be driven HIGH.
bool relayLogicalToPhysicalHigh(bool logical_on, bool active_low);

// Renders `state` (bit i = channel i's logical ON/OFF) as 4 chars '0'/'1'
// into out[0..3] plus a trailing '\0' at out[4]. Position i (left to right)
// IS the channel index - NOT a standard MSB-first binary rendering of the
// byte.
void buildRelayStateBits(uint8_t state, char out[5]);
```

- [ ] **Step 2: Write the failing tests**

```cpp
// test/test_gpio_relay_logic/test_gpio_relay_logic.cpp
#include <gtest/gtest.h>
#include "helpers/actuators/GPIORelayLogic.h"

TEST(RelayLogicalToPhysicalHigh, ActiveHighOnDrivesHigh) {
  EXPECT_TRUE(relayLogicalToPhysicalHigh(true, false));
}

TEST(RelayLogicalToPhysicalHigh, ActiveHighOffDrivesLow) {
  EXPECT_FALSE(relayLogicalToPhysicalHigh(false, false));
}

TEST(RelayLogicalToPhysicalHigh, ActiveLowOnDrivesLow) {
  EXPECT_FALSE(relayLogicalToPhysicalHigh(true, true));
}

TEST(RelayLogicalToPhysicalHigh, ActiveLowOffDrivesHigh) {
  EXPECT_TRUE(relayLogicalToPhysicalHigh(false, true));
}

TEST(BuildRelayStateBits, RendersChannelsZeroAndOneOn) {
  char out[5];
  buildRelayStateBits(0b0011, out);
  EXPECT_STREQ("1100", out);
}

TEST(BuildRelayStateBits, RendersChannelsTwoAndThreeOn) {
  char out[5];
  buildRelayStateBits(0b1100, out);
  EXPECT_STREQ("0011", out);
}

TEST(BuildRelayStateBits, RendersAllOff) {
  char out[5];
  buildRelayStateBits(0, out);
  EXPECT_STREQ("0000", out);
}

TEST(BuildRelayStateBits, RendersAllOn) {
  char out[5];
  buildRelayStateBits(0b1111, out);
  EXPECT_STREQ("1111", out);
}
```

- [ ] **Step 3: Add the file to the native test build and run it to verify it fails**

Edit `platformio.ini` `[env:native]` `build_src_filter` (currently at lines 168-172) to add the new logic file:

```ini
build_src_filter =
  -<*>
  +<../src/Utils.cpp>
  +<../src/Packet.cpp>
  +<../src/helpers/ConfigSerializer.cpp>
  +<../src/helpers/actuators/GPIORelayLogic.cpp>
```

Run: `pio test -e native -f test_gpio_relay_logic`
Expected: FAIL to build (`GPIORelayLogic.cpp` does not exist yet, or link errors for undefined functions).

- [ ] **Step 4: Implement the two functions**

```cpp
// src/helpers/actuators/GPIORelayLogic.cpp
#include "GPIORelayLogic.h"

bool relayLogicalToPhysicalHigh(bool logical_on, bool active_low) {
  return active_low ? !logical_on : logical_on;
}

void buildRelayStateBits(uint8_t state, char out[5]) {
  for (uint8_t i = 0; i <= RELAY_MAX_PIN; i++) {
    out[i] = (state & (1 << i)) ? '1' : '0';
  }
  out[RELAY_MAX_PIN + 1] = 0;
}
```

- [ ] **Step 5: Run the tests and verify they pass**

Run: `pio test -e native -f test_gpio_relay_logic`
Expected: PASS, all 8 tests green.

- [ ] **Step 6: Commit**

```bash
git add src/helpers/actuators/GPIORelayLogic.h src/helpers/actuators/GPIORelayLogic.cpp test/test_gpio_relay_logic/test_gpio_relay_logic.cpp platformio.ini
git commit -m "Add relay polarity translation and state-bit rendering logic"
```

---

### Task 2: Pure relay logic — command parsing (TDD)

**Files:**
- Modify: `src/helpers/actuators/GPIORelayLogic.h`
- Modify: `src/helpers/actuators/GPIORelayLogic.cpp`
- Modify: `test/test_gpio_relay_logic/test_gpio_relay_logic.cpp`

**Interfaces:**
- Consumes: `RELAY_MAX_PIN` from Task 1.
- Produces: `bool parseRelayActuatorCmd(const char* text, const char* prefix, const char* on_suffix, const char* off_suffix, uint8_t* pin, bool* state)`; `bool relayTextEndsWithCmd(const char* text, const char* cmd)`. Both consumed later by `MyMesh::checkActuatorCommand` (Task 5).

- [ ] **Step 1: Add declarations to the header**

Append to `src/helpers/actuators/GPIORelayLogic.h` (after `buildRelayStateBits`'s declaration):

```cpp
// Parses a "<prefix><digit 0-RELAY_MAX_PIN><suffix>" command as a SUFFIX of
// `text` (companion group messages are sent as "<sender name>: <text>", so
// the command is matched at the end of the string, not as an exact match).
// suffix is on_suffix or off_suffix; on match, fills *pin and *state
// (true = on_suffix matched) and returns true.
bool parseRelayActuatorCmd(const char* text, const char* prefix, const char* on_suffix, const char* off_suffix,
                            uint8_t* pin, bool* state);

// Matches a literal command word (e.g. "PIN_STATUS") as a suffix of `text`,
// same suffix-matching rationale as parseRelayActuatorCmd, no digit involved.
bool relayTextEndsWithCmd(const char* text, const char* cmd);
```

- [ ] **Step 2: Write the failing tests**

Append to `test/test_gpio_relay_logic/test_gpio_relay_logic.cpp`:

```cpp
TEST(ParseRelayActuatorCmd, MatchesOnCommandAsSuffix) {
  uint8_t pin; bool state;
  ASSERT_TRUE(parseRelayActuatorCmd("Node1: PIN2_ON", "PIN", "_ON", "_OFF", &pin, &state));
  EXPECT_EQ(2, pin);
  EXPECT_TRUE(state);
}

TEST(ParseRelayActuatorCmd, MatchesOffCommandAsSuffix) {
  uint8_t pin; bool state;
  ASSERT_TRUE(parseRelayActuatorCmd("Node1: PIN0_OFF", "PIN", "_ON", "_OFF", &pin, &state));
  EXPECT_EQ(0, pin);
  EXPECT_FALSE(state);
}

TEST(ParseRelayActuatorCmd, RejectsDigitAboveMaxPin) {
  uint8_t pin; bool state;
  EXPECT_FALSE(parseRelayActuatorCmd("Node1: PIN4_ON", "PIN", "_ON", "_OFF", &pin, &state));
}

TEST(ParseRelayActuatorCmd, RejectsWrongPrefix) {
  uint8_t pin; bool state;
  EXPECT_FALSE(parseRelayActuatorCmd("Node1: LED2_ON", "PIN", "_ON", "_OFF", &pin, &state));
}

TEST(ParseRelayActuatorCmd, RejectsTextShorterThanCommand) {
  uint8_t pin; bool state;
  EXPECT_FALSE(parseRelayActuatorCmd("ON", "PIN", "_ON", "_OFF", &pin, &state));
}

TEST(RelayTextEndsWithCmd, MatchesStatusCommandAsSuffix) {
  EXPECT_TRUE(relayTextEndsWithCmd("Node1: PIN_STATUS", "PIN_STATUS"));
}

TEST(RelayTextEndsWithCmd, RejectsNonMatchingSuffix) {
  EXPECT_FALSE(relayTextEndsWithCmd("Node1: PIN0_ON", "PIN_STATUS"));
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native -f test_gpio_relay_logic`
Expected: FAIL to build (`parseRelayActuatorCmd`/`relayTextEndsWithCmd` undefined).

- [ ] **Step 4: Implement both functions**

Append to `src/helpers/actuators/GPIORelayLogic.cpp`:

```cpp
bool parseRelayActuatorCmd(const char* text, const char* prefix, const char* on_suffix, const char* off_suffix,
                            uint8_t* pin, bool* state) {
  size_t text_len = strlen(text);
  size_t prefix_len = strlen(prefix);

  for (int i = 0; i < 2; i++) {
    const char* suffix = (i == 0) ? on_suffix : off_suffix;
    size_t suffix_len = strlen(suffix);
    size_t cmd_len = prefix_len + 1 + suffix_len;   // prefix + 1 digit + suffix
    if (cmd_len > text_len) continue;

    const char* cmd = text + (text_len - cmd_len);
    if (memcmp(cmd, prefix, prefix_len) != 0) continue;

    char digit = cmd[prefix_len];
    if (digit < '0' || digit > '0' + RELAY_MAX_PIN) continue;
    if (strcmp(cmd + prefix_len + 1, suffix) != 0) continue;

    *pin = (uint8_t)(digit - '0');
    *state = (i == 0);
    return true;
  }
  return false;
}

bool relayTextEndsWithCmd(const char* text, const char* cmd) {
  size_t text_len = strlen(text);
  size_t cmd_len = strlen(cmd);
  if (cmd_len > text_len) return false;
  return strcmp(text + (text_len - cmd_len), cmd) == 0;
}
```

- [ ] **Step 5: Run the tests and verify they pass**

Run: `pio test -e native -f test_gpio_relay_logic`
Expected: PASS, all 15 tests green (8 from Task 1 + 7 new).

- [ ] **Step 6: Commit**

```bash
git add src/helpers/actuators/GPIORelayLogic.h src/helpers/actuators/GPIORelayLogic.cpp test/test_gpio_relay_logic/test_gpio_relay_logic.cpp
git commit -m "Add relay command parsing logic"
```

---

### Task 3: `GPIORelayActuator` driver + platformio.ini wiring

**Files:**
- Create: `src/helpers/actuators/GPIORelayActuator.h`
- Create: `src/helpers/actuators/GPIORelayActuator.cpp`
- Modify: `variants/lilygo_t3s3_sx1276/platformio.ini`

**Interfaces:**
- Consumes: `RELAY_MAX_PIN`, `relayLogicalToPhysicalHigh()` from Task 1.
- Produces: class `GPIORelayActuator` with `static void earlyInit()`, `void begin()`, `bool setPin(uint8_t pin, bool state)`, `uint8_t getState() const`. `earlyInit()` consumed by `LilygoT3S3SX1276Board::begin()` (Task 4). The instance methods consumed by `MyMesh` (Task 5).

This task has no native unit test — it depends on `Arduino.h` (`pinMode`/`digitalWrite`), which only exists in the real ESP32 build. Verification is a full PlatformIO env compile (Step 4).

- [ ] **Step 1: Create the driver header**

```cpp
// src/helpers/actuators/GPIORelayActuator.h
#pragma once

#ifdef HAS_GPIO_RELAY_ACTUATOR

#include <Arduino.h>
#include "GPIORelayLogic.h"

#ifndef RELAY_PIN0
#define RELAY_PIN0 16
#endif
#ifndef RELAY_PIN1
#define RELAY_PIN1 15
#endif
#ifndef RELAY_PIN2
#define RELAY_PIN2 39
#endif
#ifndef RELAY_PIN3
#define RELAY_PIN3 40
#endif

#ifdef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW_BOOL true
#else
#define RELAY_ACTIVE_LOW_BOOL false
#endif

/*
 * Direct-GPIO driver for a 4-channel relay module (e.g. SRD-05VDC-SL-C
 * based boards). Unlike an I2C-expander actuator, there is no external
 * chip that can diverge from what the firmware last wrote - the GPIO
 * output register IS the state - so this driver only ever needs a plain
 * in-memory cache, no bus read-back.
 */
class GPIORelayActuator {
public:
  // Drives all 4 channels to the safe OFF level immediately. Stateless and
  // idempotent - intended to be called from Board::begin(), before any
  // GPIORelayActuator instance exists, to minimize the boot-time window
  // where a floating pin could be read as "energized" by an active-low
  // relay board. Safe to call again later (begin() does).
  static void earlyInit();

  void begin();                              // idempotent re-init + resets cache
  bool setPin(uint8_t pin, bool state);      // logical ON/OFF; always succeeds (no bus to fail)
  uint8_t getState() const { return _state; }

private:
  uint8_t _state = 0x00;   // logical cache, bit i = channel i, safe default = all OFF
};

#endif // HAS_GPIO_RELAY_ACTUATOR
```

- [ ] **Step 2: Create the driver implementation**

```cpp
// src/helpers/actuators/GPIORelayActuator.cpp
#include "GPIORelayActuator.h"

#ifdef HAS_GPIO_RELAY_ACTUATOR

static const uint8_t RELAY_PINS[RELAY_MAX_PIN + 1] = { RELAY_PIN0, RELAY_PIN1, RELAY_PIN2, RELAY_PIN3 };

static void writeRelayPin(uint8_t pin, bool logical_on) {
  bool physical_high = relayLogicalToPhysicalHigh(logical_on, RELAY_ACTIVE_LOW_BOOL);
  digitalWrite(RELAY_PINS[pin], physical_high ? HIGH : LOW);
}

void GPIORelayActuator::earlyInit() {
  for (uint8_t i = 0; i <= RELAY_MAX_PIN; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    writeRelayPin(i, false);   // safe default: all channels OFF
  }
}

void GPIORelayActuator::begin() {
  earlyInit();
  _state = 0x00;
}

bool GPIORelayActuator::setPin(uint8_t pin, bool state) {
  if (pin > RELAY_MAX_PIN) return false;
  writeRelayPin(pin, state);
  if (state) {
    _state |= (uint8_t)(1 << pin);
  } else {
    _state &= (uint8_t)~(1 << pin);
  }
  return true;
}

#endif // HAS_GPIO_RELAY_ACTUATOR
```

- [ ] **Step 3: Wire the build flags and source filter in `variants/lilygo_t3s3_sx1276/platformio.ini`**

In `[env:LilyGo_T3S3_sx1276_companion_radio_ble]` (currently lines 151-172), add to `build_flags` (after `-D OFFLINE_QUEUE_SIZE=256`):

```ini
  -D HAS_GPIO_RELAY_ACTUATOR=1
  -D RELAY_PIN0=16
  -D RELAY_PIN1=15
  -D RELAY_PIN2=39
  -D RELAY_PIN3=40
  -D RELAY_ACTIVE_LOW=1
;  -D ACTUATOR_CMD_PREFIX='"PIN"'
;  -D ACTUATOR_CMD_ON_SUFFIX='"_ON"'
;  -D ACTUATOR_CMD_OFF_SUFFIX='"_OFF"'
;  -D ACTUATOR_CMD_STATUS='"PIN_STATUS"'
;  -D ACTUATOR_SEND_ACK=1
```

And add to that same env's `build_src_filter` (currently lines 164-169):

```ini
build_src_filter = ${LilyGo_T3S3_sx1276.build_src_filter}
  +<helpers/esp32/*.cpp>
  +<helpers/ui/SSD1306Display.cpp>
  +<helpers/ui/MomentaryButton.cpp>
  +<../examples/companion_radio/*.cpp>
  +<../examples/companion_radio/ui-new/*.cpp>
  +<helpers/actuators/GPIORelayLogic.cpp>
  +<helpers/actuators/GPIORelayActuator.cpp>
```

- [ ] **Step 4: Compile to verify the driver builds**

Run: `pio run -e LilyGo_T3S3_sx1276_companion_radio_ble`
Expected: build succeeds (this env doesn't yet call any of the new symbols, so this only proves the new files compile standalone under the ESP32 toolchain and the `build_src_filter` picked them up — check the build log for `GPIORelayActuator.cpp` and `GPIORelayLogic.cpp` in the compiled object list).

- [ ] **Step 5: Commit**

```bash
git add src/helpers/actuators/GPIORelayActuator.h src/helpers/actuators/GPIORelayActuator.cpp variants/lilygo_t3s3_sx1276/platformio.ini
git commit -m "Add GPIORelayActuator driver and wire it into the T3S3 SX1276 BLE env"
```

---

### Task 4: Boot-safety in `LilygoT3S3SX1276Board`

**Files:**
- Modify: `variants/lilygo_t3s3_sx1276/LilygoT3S3SX1276Board.h`

**Interfaces:**
- Consumes: `GPIORelayActuator::earlyInit()` from Task 3.

- [ ] **Step 1: Add the boot-safety override**

Current full content of `variants/lilygo_t3s3_sx1276/LilygoT3S3SX1276Board.h`:

```cpp
#pragma once

#include <helpers/ESP32Board.h>

class LilygoT3S3SX1276Board : public ESP32Board {
public:
  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0; // default for SX1276
  }
};
```

Replace it with:

```cpp
#pragma once

#include <helpers/ESP32Board.h>

#ifdef HAS_GPIO_RELAY_ACTUATOR
#include <helpers/actuators/GPIORelayActuator.h>
#endif

class LilygoT3S3SX1276Board : public ESP32Board {
public:
  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0; // default for SX1276
  }

#ifdef HAS_GPIO_RELAY_ACTUATOR
  // ESP32Board::begin() is not virtual (see its own comment: subclasses
  // SHOULD call it from their begin()) - this hides it via static dispatch,
  // which works because `board` in main.cpp is declared as the concrete
  // LilygoT3S3SX1276Board type, not a Board*/MainBoard* pointer.
  //
  // Drives all 4 relay channels to a safe OFF level as the very first
  // action of setup() (board.begin() is the first call in main.cpp's
  // setup(), before radio/display/BLE init) - minimizes the window where a
  // floating GPIO could be read as "energized" by an active-low relay
  // board. See docs/superpowers/specs/2026-08-31-t3s3-gpio-relay-actuator-design.md.
  void begin() {
    ESP32Board::begin();
    GPIORelayActuator::earlyInit();
  }
#endif
};
```

- [ ] **Step 2: Compile to verify it builds**

Run: `pio run -e LilyGo_T3S3_sx1276_companion_radio_ble`
Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add variants/lilygo_t3s3_sx1276/LilygoT3S3SX1276Board.h
git commit -m "Drive relay channels to safe OFF as the first action of T3S3 boot"
```

---

### Task 5: `MyMesh` channel-command integration

**Files:**
- Modify: `examples/companion_radio/MyMesh.h`
- Modify: `examples/companion_radio/MyMesh.cpp`

**Interfaces:**
- Consumes: `GPIORelayActuator` (Task 3); `parseRelayActuatorCmd`, `relayTextEndsWithCmd`, `buildRelayStateBits` (Tasks 1-2); `ChannelDetails`, `findChannelIdx`, `getChannel`, `sendGroupMessage`, `getRTCClock` (existing `BaseChatMesh`/`MyMesh` members).
- Produces: `MyMesh::checkActuatorCommand(const mesh::GroupChannel&, const char*)`, hooked into `MyMesh::onChannelMessageRecv`.

- [ ] **Step 1: Add the include and the member/declaration to `MyMesh.h`**

In `examples/companion_radio/MyMesh.h`, after line 34 (`#include <helpers/StaticPoolPacketManager.h>`), add:

```cpp
#ifdef HAS_GPIO_RELAY_ACTUATOR
#include <helpers/actuators/GPIORelayActuator.h>
#endif
```

In the `private:` section starting at line 188, add (right after `private:`):

```cpp
#ifdef HAS_GPIO_RELAY_ACTUATOR
  GPIORelayActuator actuator;
  void checkActuatorCommand(const mesh::GroupChannel& channel, const char* text);
#endif
```

- [ ] **Step 2: Add `checkActuatorCommand()` and its static helpers to `MyMesh.cpp`**

Insert immediately before `void MyMesh::onChannelMessageRecv(...)` (currently starting at line 545):

```cpp
#ifdef HAS_GPIO_RELAY_ACTUATOR
#include <helpers/actuators/GPIORelayLogic.h>

#ifndef ACTUATOR_CMD_PREFIX
#define ACTUATOR_CMD_PREFIX "PIN"
#endif
#ifndef ACTUATOR_CMD_ON_SUFFIX
#define ACTUATOR_CMD_ON_SUFFIX "_ON"
#endif
#ifndef ACTUATOR_CMD_OFF_SUFFIX
#define ACTUATOR_CMD_OFF_SUFFIX "_OFF"
#endif
#ifndef ACTUATOR_CMD_STATUS
#define ACTUATOR_CMD_STATUS "PIN_STATUS"
#endif

// only channels whose secret is the sha256("<name>")[:16] hashtag-channel
// key are authorized to trigger the actuator - excludes both the shared
// "Public" channel and any private (randomly-keyed) channel.
static bool isHashtagChannel(const char* name, const mesh::GroupChannel& channel) {
  uint8_t expected[16];
  mesh::Utils::sha256(expected, sizeof(expected), (const uint8_t*)name, strlen(name));
  return memcmp(expected, channel.secret, sizeof(expected)) == 0;
}

void MyMesh::checkActuatorCommand(const mesh::GroupChannel& channel, const char* text) {
  uint8_t pin;
  bool state;
  bool is_write = parseRelayActuatorCmd(text, ACTUATOR_CMD_PREFIX, ACTUATOR_CMD_ON_SUFFIX, ACTUATOR_CMD_OFF_SUFFIX, &pin, &state);
  bool is_status = !is_write && relayTextEndsWithCmd(text, ACTUATOR_CMD_STATUS);
  if (!is_write && !is_status) {
    return;   // not an actuator command
  }

  int idx = findChannelIdx(channel);
  ChannelDetails details;
  if (idx < 0 || !getChannel(idx, details) || !isHashtagChannel(details.name, channel)) {
    MESH_DEBUG_PRINTLN("checkActuatorCommand: keyword matched but channel is not an authorized hashtag channel, ignoring");
    return;
  }

  char bits[5];

  if (is_write) {
    MESH_DEBUG_PRINTLN("checkActuatorCommand: keyword matched, setting relay %d to %d", (uint32_t)pin, (uint32_t)state);
    bool did_act = actuator.setPin(pin, state);
#ifdef ACTUATOR_SEND_ACK
    if (did_act) {
      buildRelayStateBits(actuator.getState(), bits);
      char msg[32];
      snprintf(msg, sizeof(msg), "PIN%u=%s STATE=b%s", (unsigned)pin, state ? "ON" : "OFF", bits);
      sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
    }
#else
    (void)did_act;
#endif
  } else {   // is_status
    MESH_DEBUG_PRINTLN("checkActuatorCommand: status query matched");
    buildRelayStateBits(actuator.getState(), bits);
    char msg[24];
    snprintf(msg, sizeof(msg), "STATE=b%s", bits);
    sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
  }
}
#endif // HAS_GPIO_RELAY_ACTUATOR

```

- [ ] **Step 3: Hook the command check into `onChannelMessageRecv`**

In `examples/companion_radio/MyMesh.cpp`, at the top of `MyMesh::onChannelMessageRecv` (currently lines 545-547), change:

```cpp
void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
```

to:

```cpp
void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
#ifdef HAS_GPIO_RELAY_ACTUATOR
  checkActuatorCommand(channel, text);
#endif

  int i = 0;
```

- [ ] **Step 4: Call `actuator.begin()` from `MyMesh::begin()`**

In `examples/companion_radio/MyMesh.cpp`, change (currently lines 901-902):

```cpp
void MyMesh::begin(bool has_display) {
  BaseChatMesh::begin();
```

to:

```cpp
void MyMesh::begin(bool has_display) {
  BaseChatMesh::begin();

#ifdef HAS_GPIO_RELAY_ACTUATOR
  actuator.begin();
#endif
```

- [ ] **Step 5: Compile to verify it builds, both without and with the ack flag**

Run: `pio run -e LilyGo_T3S3_sx1276_companion_radio_ble`
Expected: build succeeds (default config, no `ACTUATOR_SEND_ACK`).

Temporarily add `-D ACTUATOR_SEND_ACK=1` to the env's `build_flags` in `variants/lilygo_t3s3_sx1276/platformio.ini`, then:

Run: `pio run -e LilyGo_T3S3_sx1276_companion_radio_ble`
Expected: build succeeds (verifies the `#ifdef ACTUATOR_SEND_ACK` branch compiles).

Remove the temporary flag (restore the commented-out `;  -D ACTUATOR_SEND_ACK=1` line from Task 3) before committing.

- [ ] **Step 6: Commit**

```bash
git add examples/companion_radio/MyMesh.h examples/companion_radio/MyMesh.cpp
git commit -m "Wire GPIO relay actuator commands into MyMesh channel message handling"
```

---

### Task 6: Documentation

**Files:**
- Create: `README_RELAY_GPIO.md`

- [ ] **Step 1: Write the README**

```markdown
# Actuador de relay de 4 canales por GPIO directo (LilyGo T3S3 SX1276)

Esta variante del firmware companion de MeshCore añade una funcionalidad
adicional: cuando el nodo recibe, en un canal de grupo, un mensaje de texto
que coincide con un patrón de comando configurable (`PIN<n>_ON` /
`PIN<n>_OFF`), activa o desactiva el canal `<n>` (0-3) de un relay de 4
canales conectado directamente a 4 pines GPIO de la placa — sin chip
expansor I2C intermedio.

- **Target de hardware**: LilyGo T3S3 con módulo LoRa SX1276 (entorno
  PlatformIO `LilyGo_T3S3_sx1276_companion_radio_ble`).
- **Punto de enganche en el código**: `MyMesh::onChannelMessageRecv()` en
  `examples/companion_radio/MyMesh.cpp`.
- **Driver del actuador**: `src/helpers/actuators/GPIORelayActuator.h/.cpp`
  (lógica pura y testeada nativamente en
  `src/helpers/actuators/GPIORelayLogic.h/.cpp`).

## Cómo funciona

1. Cualquier persona que conozca la clave del canal envía un mensaje de
   texto al canal (ej. `PIN2_ON` para activar el canal 2). El firmware
   compara el comando como **sufijo** del mensaje, porque el texto real que
   viaja por la malla siempre lleva el prefijo `"<nombre_nodo_emisor>: "`.
2. El firmware valida que el canal por el que llegó sea un **canal hashtag
   autorizado** (secreto = `sha256(nombre_del_canal)[:16]`) antes de actuar
   — esto excluye automáticamente el canal `Public` y cualquier canal
   privado de clave aleatoria.
3. Si pasa la validación, el firmware escribe directamente sobre el pin GPIO
   correspondiente (`digitalWrite`), traduciendo el estado lógico ON/OFF al
   nivel eléctrico correcto según la polaridad configurada.
4. `PIN_STATUS` (siempre disponible, sin flag) responde con el estado
   lógico de los 4 canales: `STATE=b0000`..`STATE=b1111` (posición =
   índice de canal, izquierda a derecha).
5. Por defecto no hay respuesta por la malla. Con el flag opcional
   `ACTUATOR_SEND_ACK`, cada escritura exitosa genera un ack:
   `PIN2=ON STATE=b0100`.

## Pines y polaridad

| Canal | GPIO | Build flag |
|---|---|---|
| 0 | 16 | `RELAY_PIN0` |
| 1 | 15 | `RELAY_PIN1` |
| 2 | 39 | `RELAY_PIN2` |
| 3 | 40 | `RELAY_PIN3` |

`RELAY_ACTIVE_LOW=1` está definido por defecto: GPIO en LOW energiza el
relé (módulos típicos basados en `SRD-05VDC-SL-C`). Si tu módulo es
active-high, quita ese flag antes de operar con cargas reales.

**Verificación física obligatoria antes de energizar cargas**: estos 4
pines fueron elegidos por eliminación contra los build flags ya usados por
este board (LoRa, botón, I2C del display/RTC) y contra el pinout público del
T3S3 — no contra el header físico real de tu unidad. Confirma con
multímetro que cada GPIO llega al conector que vas a usar, y confirma la
polaridad de tu módulo de relay concreto antes de conectar cargas.

GPIO18, aunque a veces se lista como "libre" en documentación genérica del
ESP32-S3, está reservado en este proyecto como `PIN_BOARD_SDA` (bus I2C
compartido por el display OLED y el RTC) — no lo reutilices para el relay
en este env.

## Configurar el canal y el patrón de comando

Igual que el resto de actuadores de MeshCore por comando de canal: crea un
canal cuyo nombre empiece con `#` desde la app companion (ver
`docs/companion_protocol.md`), y opcionalmente ajusta el prefijo/sufijos en
`variants/lilygo_t3s3_sx1276/platformio.ini`:

```ini
-D ACTUATOR_CMD_PREFIX='"PIN"'
-D ACTUATOR_CMD_ON_SUFFIX='"_ON"'
-D ACTUATOR_CMD_OFF_SUFFIX='"_OFF"'
-D ACTUATOR_CMD_STATUS='"PIN_STATUS"'
-D ACTUATOR_SEND_ACK=1
```

## Ver también

- `docs/superpowers/specs/2026-08-31-t3s3-gpio-relay-actuator-design.md` —
  spec de diseño completa, incluyendo las decisiones sobre pines y
  polaridad.
```

- [ ] **Step 2: Commit**

```bash
git add README_RELAY_GPIO.md
git commit -m "Document the T3S3 GPIO relay actuator"
```
