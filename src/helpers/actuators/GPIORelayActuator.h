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
