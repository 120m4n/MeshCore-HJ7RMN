#pragma once
#include <cstdint>

namespace tracecore {

// Limita el tiempo total de TX en una ventana deslizante (FR-007).
// Cuenta la duración completa de cada TX cuyo inicio cae dentro de la ventana.
class DutyLimiter {
 public:
  explicit DutyLimiter(uint32_t window_ms = 60000, uint32_t max_on_ms = 6000)
      : window_(window_ms), max_on_(max_on_ms) {}

  // Primer instante ≥ now en que una TX de `dur_ms` cabe en el presupuesto.
  uint32_t nextAllowed(uint32_t now, uint32_t dur_ms);
  bool canTransmit(uint32_t now, uint32_t dur_ms) { return nextAllowed(now, dur_ms) == now; }
  void record(uint32_t start_ms, uint32_t dur_ms);

 private:
  static constexpr uint8_t kRing = 64;
  struct Entry { uint32_t start, dur; };
  void purge(uint32_t now);
  const Entry& at(uint8_t k) const { return ring_[(head_ + k) % kRing]; }

  uint32_t window_, max_on_;
  Entry ring_[kRing] = {};
  uint8_t head_ = 0, count_ = 0;
};

}  // namespace tracecore
