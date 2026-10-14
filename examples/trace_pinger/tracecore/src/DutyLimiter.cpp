#include "tracecore/DutyLimiter.h"

namespace tracecore {

void DutyLimiter::purge(uint32_t now) {
  while (count_ && (uint32_t)(now - ring_[head_].start) >= window_) {
    head_ = (head_ + 1) % kRing;
    count_--;
  }
}

uint32_t DutyLimiter::nextAllowed(uint32_t now, uint32_t dur_ms) {
  purge(now);
  uint32_t used = 0;
  for (uint8_t k = 0; k < count_; k++) used += at(k).dur;
  uint32_t t = now;
  for (uint8_t k = 0; used + dur_ms > max_on_ && k < count_; k++) {
    used -= at(k).dur;
    const uint32_t expires = at(k).start + window_;
    if ((int32_t)(expires - t) > 0) t = expires;
  }
  return t;
}

void DutyLimiter::record(uint32_t start_ms, uint32_t dur_ms) {
  purge(start_ms);
  if (count_ == kRing) { head_ = (head_ + 1) % kRing; count_--; }  // ponytail: 64 entradas bastan con rate ≥ 1 s
  ring_[(head_ + count_) % kRing] = {start_ms, dur_ms};
  count_++;
}

}  // namespace tracecore
