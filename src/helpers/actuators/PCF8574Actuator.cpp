#ifdef HAS_PCF8574_ACTUATOR

#include "PCF8574Actuator.h"
#include <MeshCore.h>

void PCF8574Actuator::begin(TwoWire& wire, uint8_t i2c_addr) {
  _wire = &wire;
  _addr = i2c_addr;
  _out_state = 0xFF;
  writeState();
}

bool PCF8574Actuator::setPin(uint8_t pin, bool state) {
  if (pin > PCF8574_MAX_PIN) {
    MESH_DEBUG_PRINTLN("PCF8574Actuator: pin %d out of range (max %d)", (uint32_t)pin, (uint32_t)PCF8574_MAX_PIN);
    return false;
  }
  if (_wire == NULL) return false;

  if (state) {
    _out_state |= (1 << pin);
  } else {
    _out_state &= ~(1 << pin);
  }
  return writeState();
}

bool PCF8574Actuator::readState(uint8_t* out, bool* drifted) {
  if (drifted != NULL) *drifted = false;

  if (_wire == NULL || _wire->requestFrom((uint8_t)_addr, (uint8_t)1) != 1) {
    MESH_DEBUG_PRINTLN("PCF8574Actuator: I2C read failed");
    *out = _out_state;
    return false;
  }

  uint8_t actual = _wire->read();
  if (actual != _out_state) {
    MESH_DEBUG_PRINTLN("PCF8574Actuator: state drift detected, chip=0x%02X cached=0x%02X, resyncing", (uint32_t)actual, (uint32_t)_out_state);
    if (drifted != NULL) *drifted = true;
  }
  _out_state = actual;   // chip is ground truth
  *out = actual;
  return true;
}

bool PCF8574Actuator::writeState() {
  _wire->beginTransmission(_addr);
  _wire->write(_out_state);
  uint8_t err = _wire->endTransmission();
  if (err != 0) {
    MESH_DEBUG_PRINTLN("PCF8574Actuator: I2C write failed, err=%d", (uint32_t)err);
    return false;
  }
  return true;
}

#endif // ifdef HAS_PCF8574_ACTUATOR
