#include "tracecore/Stats.h"

namespace tracecore {

bool median(const int32_t* v, size_t n, int32_t& out) {
  if (n == 0 || n > kMaxBurst) return false;
  int32_t s[kMaxBurst];
  for (size_t i = 0; i < n; i++) {  // inserción: n ≤ 50
    size_t j = i;
    while (j > 0 && s[j - 1] > v[i]) { s[j] = s[j - 1]; j--; }
    s[j] = v[i];
  }
  if (n % 2) { out = s[n / 2]; return true; }
  const int32_t sum = s[n / 2 - 1] + s[n / 2];
  out = (sum >= 0 ? sum + 1 : sum - 1) / 2;
  return true;
}

}  // namespace tracecore
