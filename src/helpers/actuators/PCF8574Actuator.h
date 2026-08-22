#pragma once

#ifdef HAS_PCF8574_ACTUATOR

#include <Arduino.h>
#include <Wire.h>

#ifndef PCF8574_I2C_ADDR
#define PCF8574_I2C_ADDR 0x20
#endif

#define PCF8574_MAX_PIN 7   // PCF8574 has 8 pins, addressed 0-7

/*
 * Minimal driver for a PCF8574 I2C GPIO expander used to drive an
 * external actuator (relay, etc.) from a companion radio command.
 * The PCF8574 has no internal registers: writing a byte sets all 8
 * quasi-bidirectional pins at once, so the driver tracks the current
 * pin states itself and rewrites the whole byte on each change.
 */
class PCF8574Actuator {
public:
  void begin(TwoWire& wire, uint8_t i2c_addr = PCF8574_I2C_ADDR);
  bool setPin(uint8_t pin, bool state);   // returns false on I2C error
  uint8_t getState() const { return _out_state; }   // last-written byte, no I2C traffic

  // live I2C read of the chip's actual pin state. On success, resyncs the
  // cache to match (the chip is ground truth - e.g. it may have lost power
  // on its own supply rail independently of the host). Returns false only
  // on I2C error, in which case *out is the last-known cached value.
  bool readState(uint8_t* out, bool* drifted = NULL);

private:
  TwoWire* _wire = NULL;
  uint8_t _addr = PCF8574_I2C_ADDR;
  // Safe default until begin() confirms the chip's real state (or a chip
  // never answers): no pin assumed on. Do not change this back to 0xFF -
  // that reintroduces an assumed all-HIGH state whenever the boot-time
  // readState() in begin() fails to get an I2C ack (e.g. the actuator board
  // still booting when the companion powers up on a shared rail).
  uint8_t _out_state = 0x00;

  bool writeState();
};

#endif // ifdef HAS_PCF8574_ACTUATOR
