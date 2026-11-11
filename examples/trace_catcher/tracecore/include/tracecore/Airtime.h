#pragma once
#include <cstdint>

#include "tracecore/Config.h"

namespace tracecore {

constexpr uint8_t kRequestLen = 12;   // con hash de 1 byte; con hash de N bytes: 11 + N
constexpr uint8_t kEchoLen = 13;      // con hash de 1 byte; con hash de N bytes: 12 + N
constexpr uint8_t kRequestMax = 15;   // hash de 4 bytes

// Tiempo en el aire (ms), fórmula de Semtech: header explícito, CRC on,
// LDRO automático si Tsym > 16 ms, preámbulo según SF.
double toaMs(const RadioConfig& r, uint8_t payload_len);

// ceil(1.5 × RTT_max) + 200 ms, con
// RTT_max = ToA(12) + ToA(13) + 5 × 0.3 × ToA(15) + 20 ms   (research R-3)
// ponytail: calculado con hash de 1 byte; con 4 bytes son 3 bytes más por trama (≤ 2 símbolos),
// cubierto de sobra por el 1.5× + 200 ms. Recalcular si se usa SF12/BW bajo con timeouts justos.
uint32_t timeoutAutoMs(const RadioConfig& r);

}  // namespace tracecore
