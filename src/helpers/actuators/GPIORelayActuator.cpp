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
