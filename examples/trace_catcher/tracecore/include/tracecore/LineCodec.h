#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tracecore/Stats.h"

namespace tracecore {

constexpr size_t kMaxLine = 160;       // terminador incluido

// Entero con `decimals` decimales: fmtFixed(-625, 2) -> "-6.25". Devuelve la longitud.
size_t fmtFixed(char* out, size_t cap, int32_t v, int decimals, bool force_sign);

// M,<n>,<kept>,<mean>,<sd>,<median>,<min>,<max>   (MEAS; dBm, mean/sd con 2 decimales, el resto con 1)
size_t formatMeas(char* out, size_t cap, const RssiStats& r);

struct Command {
  enum class Kind : uint8_t { Empty, Hello, CfgQuery, Set, Meas, Unknown, Syntax };
  Kind kind = Kind::Empty;
  uint32_t n = kMeasDefault;  // Meas: lecturas (sin validar el rango)
  char key[16] = {0};         // Set
  char val[24] = {0};         // Set
};

// Parsea una línea (sin '\n'; se toleran '\r' y espacios). No valida rangos.
Command parseCommand(std::string_view line);

}  // namespace tracecore
