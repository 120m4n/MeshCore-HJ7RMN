// src/helpers/tv_batt.h — mV de batería -> % para el campo H del vector tv (fallback sin sensor I2C)
#pragma once
#include <stdint.h>

namespace tv {

// Lineal 3300..4200 mV -> 0..100. Fuera de 3000..4300 mV (p. ej. pin flotante sin
// batería, alimentado por USB) devuelve 100 = "alimentación externa".
// ponytail: curva LiPo lineal; upgrade a tabla si hace falta precisión.
constexpr uint16_t BATT_EMPTY_MV = 3300, BATT_FULL_MV = 4200;
constexpr uint16_t BATT_VALID_MIN_MV = 3000, BATT_VALID_MAX_MV = 4300;

inline uint8_t batt_pct(uint16_t mv) {
  if (mv < BATT_VALID_MIN_MV || mv > BATT_VALID_MAX_MV) return 100;
  if (mv <= BATT_EMPTY_MV) return 0;
  if (mv >= BATT_FULL_MV) return 100;
  return (uint8_t)(((uint32_t)(mv - BATT_EMPTY_MV) * 100) / (BATT_FULL_MV - BATT_EMPTY_MV));
}

}  // namespace tv
