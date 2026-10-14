#pragma once
#include <cstdint>

#include "tracecore/Config.h"

namespace tracecore {

constexpr uint8_t kRequestLen = 12;
constexpr uint8_t kEchoLen = 13;

// Tiempo en el aire (ms), fórmula de Semtech: header explícito, CRC on,
// LDRO automático si Tsym > 16 ms, preámbulo según SF.
double toaMs(const RadioConfig& r, uint8_t payload_len);

// ceil(1.5 × RTT_max) + 200 ms, con
// RTT_max = ToA(12) + ToA(13) + 5 × 0.3 × ToA(15) + 20 ms   (research R-3)
uint32_t timeoutAutoMs(const RadioConfig& r);

}  // namespace tracecore
