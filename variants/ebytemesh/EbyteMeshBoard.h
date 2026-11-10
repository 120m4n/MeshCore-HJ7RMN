#pragma once

// EbyteMesh: Ebytastic PCB (Pro Micro nRF52840 + Ebyte E22-900M30S/M33S).
// Same pinout as the faketec/ProMicro variant, so we reuse PromicroBoard and
// only change identity + battery reporting.

#include <PromicroBoard.h>

class EbyteMeshBoard : public PromicroBoard {
public:
  EbyteMeshBoard() : NRF52Board("EbyteMesh_OTA") {}

  // Board runs from external 5 V through an LDO; P0.31 has no divider and floats.
  // ponytail: report 0 mV ("no battery") instead of ADC noise; wire a divider + drop this if a battery is added.
  uint16_t getBattMilliVolts() override { return 0; }

  const char* getManufacturerName() const override { return "EbyteMesh"; }
};
