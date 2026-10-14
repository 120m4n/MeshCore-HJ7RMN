#include "tracecore/TraceFrame.h"

namespace tracecore {

void buildRequest(uint32_t tag, uint8_t hash, uint8_t out[kRequestLen]) {
  out[0] = 0x26;  // v0 | TRACE (9) | DIRECT (2)
  out[1] = 0x00;  // path_len
  for (int i = 0; i < 4; i++) out[2 + i] = (tag >> (8 * i)) & 0xFF;
  out[6] = out[7] = out[8] = out[9] = 0;  // auth_code
  out[10] = 0x00;                         // flags: ruta con hash de 1 byte
  out[11] = hash;
}

std::optional<Echo> parseEcho(const uint8_t* b, size_t n, uint8_t hash) {
  if (n != kEchoLen || b[0] != 0x26 || b[1] != 0x01 || b[11] != 0x00 || b[12] != hash)
    return std::nullopt;
  Echo e;
  e.snr_up_q = (int8_t)b[2];
  e.tag = b[3] | (b[4] << 8) | (b[5] << 16) | ((uint32_t)b[6] << 24);
  return e;
}

}  // namespace tracecore
