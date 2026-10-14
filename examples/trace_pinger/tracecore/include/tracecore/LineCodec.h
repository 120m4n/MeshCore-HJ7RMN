#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tracecore/Types.h"

namespace tracecore {

constexpr size_t kMaxLine = 160;       // terminador incluido
constexpr size_t kMaxRawBytes = 64;    // bytes de trama en una línea R

// Entero con `decimals` decimales: fmtFixed(-625, 2) -> "-6.25". Devuelve la longitud.
size_t fmtFixed(char* out, size_t cap, int32_t v, int decimals, bool force_sign);

size_t formatPing(char* out, size_t cap, const PingResult& r);
size_t formatSummary(char* out, size_t cap, const BurstSummary& s);
size_t formatRaw(char* out, size_t cap, uint32_t t_ms, int32_t rssi_d, int32_t snr_c,
                 const uint8_t* bytes, size_t n);

struct Command {
  enum class Kind : uint8_t { Empty, Hello, CfgQuery, Set, Save, Ping, Go, Stop, Mon, Unknown, Syntax };
  Kind kind = Kind::Empty;
  uint16_t n = 1;      // Ping: repeticiones
  bool has_n = false;  // Ping: se escribió `PING <n>`
  bool on = false;     // Mon
  char key[16] = {0};  // Set
  char val[24] = {0};  // Set
};

// Parsea una línea (sin '\n'; se toleran '\r' y espacios). No valida rangos.
Command parseCommand(std::string_view line);

}  // namespace tracecore
