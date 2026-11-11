#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace tracecore {

struct RadioConfig {
  uint32_t freq_khz = 915800;  // 915.800 MHz
  uint16_t bw_x100 = 25000;    // 250 kHz (kHz × 100)
  uint8_t sf = 10;
  uint8_t cr = 5;              // 4/5
  int8_t txp_dbm = 22;
  float freqMhz() const { return freq_khz / 1000.0f; }
  float bwKhz() const { return bw_x100 / 100.0f; }
  uint16_t preamble() const { return sf <= 8 ? 32 : 16; }
  static constexpr uint8_t kSyncWord = 0x12;
};

// Hash del repetidor destino del TRACE: 1 byte (repetidor stock, capturas antiguas) o 4 bytes
// (CLAUDE.md §14: evita colisiones con repetidores reales). Big-endian: "A1B2C3D4" = 0xA1B2C3D4.
struct TraceHash {
  uint32_t v = 0;
  uint8_t len = 1;
  TraceHash() = default;
  TraceHash(uint8_t b) : v(b), len(1) {}   // implícito: compatibilidad con el hash de 1 byte
  TraceHash(uint32_t v4, uint8_t n) : v(v4), len(n) {}
  bool operator==(const TraceHash& o) const { return v == o.v && len == o.len; }
};

struct CatcherConfig {
  RadioConfig radio;
  std::optional<TraceHash> hash;       // sin valor por defecto (FR-016)
  uint32_t rate_ms = 1500;
  std::optional<uint32_t> timeout_ms;  // vacío = auto
  bool raw = false;
};

enum class SetResult : uint8_t { Ok, Unknown, Range, Syntax };
struct SetOutcome {
  SetResult result;
  const char* detail;   // para Range: texto tras "!ERR range "
  bool radio_changed;   // true si hay que reconfigurar la radio
};

// Aplica `SET key=value` de forma atómica: si falla, `cfg` no cambia.
SetOutcome applySet(CatcherConfig& cfg, std::string_view key, std::string_view value);
bool isRadioKey(std::string_view key);
bool isValid(const CatcherConfig& cfg);
size_t formatCfg(char* out, size_t cap, const CatcherConfig& cfg);
const char* bwText(uint16_t x100);   // "250", "62.5"…; nullptr si no es un BW LoRa

constexpr size_t kCfgBlobSize = 31;  // 'YPNG' | v2 | campos | crc16
size_t serialize(const CatcherConfig& cfg, uint8_t out[kCfgBlobSize]);
std::optional<CatcherConfig> deserialize(const uint8_t* b, size_t n);

}  // namespace tracecore
