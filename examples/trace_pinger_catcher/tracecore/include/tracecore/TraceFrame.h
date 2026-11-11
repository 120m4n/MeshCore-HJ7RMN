#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

#include "tracecore/Airtime.h"
#include "tracecore/Config.h"

namespace tracecore {

struct Echo {
  uint32_t tag;
  int8_t snr_up_q;  // SNR de subida en cuartos de dB
};

inline uint8_t requestLen(const TraceHash& h) { return 11 + h.len; }
inline uint8_t echoLen(const TraceHash& h) { return 12 + h.len; }

// Petición TRACE de un salto (contracts/trace-frame.md): 11 + hash.len bytes (12 o 15).
// flags = 0x00 con hash de 1 byte, 0x02 con 4 bytes (bits bajos = log2 del tamaño, Mesh.cpp).
size_t buildRequest(uint32_t tag, const TraceHash& hash, uint8_t* out);

// Acepta solo ecos de 12 + hash.len bytes con header 0x26, path_len 1, los flags del tamaño
// y la ruta == hash. La comparación del tag con el pendiente la hace quien llama.
std::optional<Echo> parseEcho(const uint8_t* b, size_t n, const TraceHash& hash);

}  // namespace tracecore
