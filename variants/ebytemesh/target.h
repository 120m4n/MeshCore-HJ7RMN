#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "EbyteMeshBoard.h"
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

#include <helpers/sensors/EnvironmentSensorManager.h>

#ifndef MAX_LORA_TX_POWER
  #define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

// The E22 PA input must not be overdriven (M33S: SX1262 <= 9 dBm per datasheet).
// Clamp every setTxPower() call, including repeater CLI "set tx".
class EbyteMeshRadioWrapper : public CustomSX1262Wrapper {
public:
  using CustomSX1262Wrapper::CustomSX1262Wrapper;
  void setTxPower(int8_t dbm) {
    CustomSX1262Wrapper::setTxPower(dbm > MAX_LORA_TX_POWER ? MAX_LORA_TX_POWER : dbm);
  }
};

extern EbyteMeshBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
