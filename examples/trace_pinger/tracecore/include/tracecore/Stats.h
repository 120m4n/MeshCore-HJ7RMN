#pragma once
#include <cstddef>
#include <cstdint>

namespace tracecore {

constexpr size_t kMaxBurst = 50;

// Mediana de n ≤ 50 enteros. En n par, promedio de los dos centrales redondeado
// al más lejano de cero. Devuelve false si n == 0 o n > kMaxBurst.
bool median(const int32_t* v, size_t n, int32_t& out);

}  // namespace tracecore
