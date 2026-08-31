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

bool parseRelayActuatorCmd(const char* text, const char* prefix, const char* on_suffix, const char* off_suffix,
                            uint8_t* pin, bool* state) {
  size_t text_len = strlen(text);
  size_t prefix_len = strlen(prefix);

  for (int i = 0; i < 2; i++) {
    const char* suffix = (i == 0) ? on_suffix : off_suffix;
    size_t suffix_len = strlen(suffix);
    size_t cmd_len = prefix_len + 1 + suffix_len;   // prefix + 1 digit + suffix
    if (cmd_len > text_len) continue;

    const char* cmd = text + (text_len - cmd_len);
    if (memcmp(cmd, prefix, prefix_len) != 0) continue;

    char digit = cmd[prefix_len];
    if (digit < '0' || digit > '0' + RELAY_MAX_PIN) continue;
    if (strcmp(cmd + prefix_len + 1, suffix) != 0) continue;

    *pin = (uint8_t)(digit - '0');
    *state = (i == 0);
    return true;
  }
  return false;
}

bool relayTextEndsWithCmd(const char* text, const char* cmd) {
  size_t text_len = strlen(text);
  size_t cmd_len = strlen(cmd);
  if (cmd_len > text_len) return false;
  return strcmp(text + (text_len - cmd_len), cmd) == 0;
}
