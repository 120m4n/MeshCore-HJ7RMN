#include "AM2301Payload.h"

void decodeAM2301Payload(const uint8_t* buf, float* temp_c, float* hum_pct, uint8_t* status) {
  *status = buf[0];

  int16_t temp_x10  = (int16_t)((buf[1] << 8) | buf[2]);
  uint16_t hum_x10   = (uint16_t)((buf[3] << 8) | buf[4]);

  *temp_c  = temp_x10 / 10.0f;
  *hum_pct = hum_x10 / 10.0f;
}
