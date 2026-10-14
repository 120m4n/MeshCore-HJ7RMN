#include "tracecore/Airtime.h"

#include <cmath>

namespace tracecore {

double toaMs(const RadioConfig& r, uint8_t payload_len) {
  const double tsym = (double)(1u << r.sf) * 100.0 / r.bw_x100;  // ms
  const int de = tsym > 16.0 ? 1 : 0;                            // LDRO
  const double n_pre = r.preamble() + (r.sf < 7 ? 6.25 : 4.25);
  const int num = 8 * payload_len - 4 * r.sf + 28 + 16;          // 16 = CRC on, header explícito
  const int den = 4 * (r.sf - 2 * de);
  const int blocks = num <= 0 ? 0 : (num + den - 1) / den;
  const double n_pay = 8 + blocks * r.cr;                        // cr 5..8 = CR index + 4
  return (n_pre + n_pay) * tsym;
}

uint32_t timeoutAutoMs(const RadioConfig& r) {
  const double rtt_max = toaMs(r, 12) + toaMs(r, 13) + 5 * 0.3 * toaMs(r, 15) + 20.0;
  return (uint32_t)std::ceil(1.5 * rtt_max) + 200;
}

}  // namespace tracecore
