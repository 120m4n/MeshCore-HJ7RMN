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