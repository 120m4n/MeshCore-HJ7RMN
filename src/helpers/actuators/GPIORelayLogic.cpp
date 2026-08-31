#include "GPIORelayLogic.h"

bool relayLogicalToPhysicalHigh(bool logical_on, bool active_low) {
  return active_low ? !logical_on : logical_on;
}

void buildRelayStateBits(uint8_t state, char out[5]) {
  for (uint8_t i = 0; i <= RELAY_MAX_PIN; i++) {
    out[i] = (state & (1 << i)) ? '1' : '0';
  }
  out[RELAY_MAX_PIN + 1] = 0;
}
