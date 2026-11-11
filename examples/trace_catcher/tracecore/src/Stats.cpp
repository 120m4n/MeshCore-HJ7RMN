#include "tracecore/Stats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tracecore {

namespace {
float medianSorted(const int16_t* s, size_t n) { return n % 2 ? s[n / 2] : 0.5f * (s[n / 2 - 1] + s[n / 2]); }
}  // namespace

bool rssiStats(const int16_t* half_db, size_t n, RssiStats& out) {
  if (n < 2 || n > kMeasMax) return false;
  int16_t s[kMeasMax], dev[kMeasMax];
  std::copy(half_db, half_db + n, s);
  std::sort(s, s + n);
  const float med = medianSorted(s, n);
  for (size_t i = 0; i < n; i++) dev[i] = (int16_t)std::lround(std::fabs(s[i] - med) * 2);   // cuartos de dB
  std::sort(dev, dev + n);
  const float mad = std::max(medianSorted(dev, n) / 2, 1.0f);   // medios dB; mínimo 0,5 dB
  const float lim = 3 * 1.4826f * mad;
  double sum = 0, sum2 = 0;
  out = RssiStats();
  out.n = (uint16_t)n;
  for (size_t i = 0; i < n; i++) {
    if (std::fabs(s[i] - med) > lim) continue;
    if (!out.kept) out.min = s[i] / 2.0f;   // s ordenado: el primero que queda es el mínimo
    out.max = s[i] / 2.0f;
    out.kept++;
    sum += s[i];
    sum2 += (double)s[i] * s[i];
  }
  const double m = sum / out.kept;   // kept ≥ 1: la mediana siempre queda
  out.mean = (float)(m / 2);
  out.sd = out.kept > 1 ? (float)(std::sqrt(std::max(0.0, (sum2 - out.kept * m * m) / (out.kept - 1))) / 2) : 0;
  out.median = med / 2;
  return true;
}

}  // namespace tracecore
