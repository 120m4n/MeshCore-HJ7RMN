#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

#include "tracecore/Airtime.h"

namespace tracecore {

struct Echo {
  uint32_t tag;
  int8_t snr_up_q;  // SNR de subida en cuartos de dB
};

// Petición TRACE de un salto (contracts/trace-frame.md): 12 bytes.
void buildRequest(uint32_t tag, uint8_t hash, uint8_t out[kRequestLen]);

// Acepta solo ecos de 13 bytes con header 0x26, path_len 1, flags 0 y route[0]==hash.
// La comparación del tag con el pendiente la hace quien llama.
std::optional<Echo> parseEcho(const uint8_t* b, size_t n, uint8_t hash);

}  // namespace tracecore
