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
