#pragma once
#include <cstdint>

namespace tracecore {

enum class Status : uint8_t { Ok, Timeout, Busy, TxErr };

// Unidades enteras para no depender de printf("%f") en el nRF52:
// snr_*_c = centésimas de dB, rssi_dn_d = décimas de dBm.
struct PingResult {
  uint32_t seq = 0;
  uint32_t tag = 0;
  Status status = Status::Timeout;
  uint32_t rtt_ms = 0;
  int32_t snr_up_c = 0;
  int32_t snr_dn_c = 0;
  int32_t rssi_dn_d = 0;
  uint8_t n_resp = 0;
};

struct BurstSummary {
  uint16_t n = 0;          // pings terminados
  uint16_t ok = 0;
  uint16_t timeouts = 0;
  uint16_t busy = 0;       // busy + txerr
  bool has_median = false;
  int32_t med_up_c = 0;
  int32_t med_dn_c = 0;
  int32_t med_rssi_d = 0;
};

}  // namespace tracecore
