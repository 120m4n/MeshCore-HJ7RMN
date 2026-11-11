#pragma once
#include <cstddef>
#include <cstdint>

namespace tracecore {

// MEAS (spec 001-aba-ping-ponger, FR-011/FR-013): lecturas de RSSI instantáneo en medios dB (−130 = −65,0 dBm).
constexpr size_t kMeasMin = 16, kMeasMax = 256, kMeasDefault = 64;
struct RssiStats {
  uint16_t n = 0, kept = 0;
  float mean = 0, sd = 0, median = 0, min = 0, max = 0;   // dBm, de las lecturas que quedan tras el filtro
};
// Filtro robusto: descarta |x − mediana| > 3·1,4826·MAD (MAD mínima 0,5 dB, la cuantización): quita las ráfagas
// LoRa ajenas. Devuelve false si n < 2 o n > kMeasMax.
bool rssiStats(const int16_t* half_db, size_t n, RssiStats& out);

}  // namespace tracecore
