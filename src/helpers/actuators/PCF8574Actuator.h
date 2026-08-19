#pragma once

#ifdef HAS_PCF8574_ACTUATOR

#include <Arduino.h>
#include <Wire.h>

#ifndef PCF8574_I2C_ADDR
#define PCF8574_I2C_ADDR 0x20
#endif

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

private:
  TwoWire* _wire = NULL;
  uint8_t _addr = PCF8574_I2C_ADDR;
  uint8_t _out_state = 0xFF;   // PCF8574 pins idle HIGH

  bool writeState();
};

#endif // ifdef HAS_PCF8574_ACTUATOR
