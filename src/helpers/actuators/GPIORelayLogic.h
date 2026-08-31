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
