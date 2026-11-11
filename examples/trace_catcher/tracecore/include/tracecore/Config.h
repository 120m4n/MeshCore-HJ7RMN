#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tracecore {

// Radio del catcher. Solo `freq` se puede cambiar (SET freq=); bw/sf/cr son los del preset de la mesh.
struct RadioConfig {
  uint32_t freq_khz = 915800;  // 915.800 MHz
  uint16_t bw_x100 = 25000;    // 250 kHz (kHz × 100)
  uint8_t sf = 10;
  uint8_t cr = 5;              // 4/5
  float freqMhz() const { return freq_khz / 1000.0f; }
  float bwKhz() const { return bw_x100 / 100.0f; }
};

enum class SetResult : uint8_t { Ok, Unknown, Range, Syntax };
struct SetOutcome {
  SetResult result;
  const char* detail;   // para Range: texto tras "!ERR range "
};

// Aplica `SET key=value`; si falla, `cfg` no cambia. Solo existe la clave `freq`.
SetOutcome applySet(RadioConfig& cfg, std::string_view key, std::string_view value);
size_t formatCfg(char* out, size_t cap, const RadioConfig& cfg);
const char* bwText(uint16_t x100);   // "250", "62.5"…; nullptr si no es un BW LoRa

}  // namespace tracecore
