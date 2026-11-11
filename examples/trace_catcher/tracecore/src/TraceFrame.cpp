#include "tracecore/TraceFrame.h"

namespace tracecore {
namespace {
uint8_t flagsFor(const TraceHash& h) { return h.len == 4 ? 0x02 : 0x00; }
uint8_t hashByte(const TraceHash& h, int i) { return (h.v >> (8 * (h.len - 1 - i))) & 0xFF; }
}  // namespace

size_t buildRequest(uint32_t tag, const TraceHash& hash, uint8_t* out) {
  out[0] = 0x26;  // v0 | TRACE (9) | DIRECT (2)
  out[1] = 0x00;  // path_len
  for (int i = 0; i < 4; i++) out[2 + i] = (tag >> (8 * i)) & 0xFF;
  out[6] = out[7] = out[8] = out[9] = 0;  // auth_code
  out[10] = flagsFor(hash);
  for (int i = 0; i < hash.len; i++) out[11 + i] = hashByte(hash, i);
  return requestLen(hash);
}

std::optional<Echo> parseEcho(const uint8_t* b, size_t n, const TraceHash& hash) {
  if (n != echoLen(hash) || b[0] != 0x26 || b[1] != 0x01 || b[11] != flagsFor(hash)) return std::nullopt;
  for (int i = 0; i < hash.len; i++)
    if (b[12 + i] != hashByte(hash, i)) return std::nullopt;
  Echo e;
  e.snr_up_q = (int8_t)b[2];
  e.tag = b[3] | (b[4] << 8) | (b[5] << 16) | ((uint32_t)b[6] << 24);
  return e;
}

}  // namespace tracecore
