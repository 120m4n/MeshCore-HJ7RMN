#include "tracecore/LabFrame.h"

#include <Utils.h>

#include <cstring>

namespace tracecore {

const uint32_t kLabBwHz[10] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};

int labBwCode(float khz) {
  const float hz = khz * 1000.0f;
  for (int i = 0; i < 10; i++) {
    const float d = hz - (float)kLabBwHz[i];
    if (d < 100.0f && d > -100.0f) return i;
  }
  return -1;
}

bool labRadioValid(const LabRadio& r) {
  return r.bw_code < 10 && r.sf >= 5 && r.sf <= 12 && r.cr >= 5 && r.cr <= 8 &&
         r.freq_khz >= 100000 && r.freq_khz <= 1100000;
}

namespace {

void putBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
uint32_t getBe32(const uint8_t* p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
void putLe32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
uint32_t getLe32(const uint8_t* p) {
  return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

void putRadio(uint8_t* p, const LabRadio& r) { putLe32(p, r.freq_khz); p[4] = r.bw_code; p[5] = r.sf; p[6] = r.cr; }
LabRadio getRadio(const uint8_t* p) { return LabRadio{getLe32(p), p[4], p[5], p[6]}; }

void auth(const char* key, const uint8_t* b, size_t n, uint8_t out[kLabAuthLen]) {
  mesh::Utils::sha256(out, kLabAuthLen, (const uint8_t*)key, (int)strlen(key), b, (int)n);
}

// Añade la auth al final de b[0..n) y devuelve la longitud total.
size_t seal(const char* key, uint8_t* b, size_t n) {
  auth(key, b, n, b + n);
  return n + kLabAuthLen;
}

bool authOk(const char* key, const uint8_t* b, size_t n) {
  uint8_t want[kLabAuthLen];
  auth(key, b, n - kLabAuthLen, want);
  return memcmp(want, b + n - kLabAuthLen, kLabAuthLen) == 0;
}

// Longitud de args para cada op conocida; -1 si desconocida.
int argLen(uint8_t op) {
  switch ((LabOp)op) {
    case LabOp::Get: case LabOp::Reset: return 0;
    case LabOp::SetTx: return 1;
    case LabOp::SetRadio: return 7;
  }
  return -1;
}

constexpr size_t kCmdHead = 12;  // magic, ver, dst[4], tag[4], seq, op

}  // namespace

size_t buildCommand(const LabCommand& c, const char* key, uint8_t* out, size_t cap) {
  const int a = argLen(c.op);
  if (a < 0 || cap < kCmdHead + a + kLabAuthLen) return 0;
  out[0] = kLabMagic; out[1] = kLabVersion; putBe32(out + 2, c.dst); putBe32(out + 6, c.tag);
  out[10] = c.seq; out[11] = c.op;
  if (c.op == (uint8_t)LabOp::SetTx) out[12] = (uint8_t)c.tx_dbm;
  if (c.op == (uint8_t)LabOp::SetRadio) putRadio(out + 12, c.radio);
  return seal(key, out, kCmdHead + a);
}

std::optional<LabCommand> parseCommand(const uint8_t* b, size_t n, uint32_t my_hash, const char* key) {
  if (n < kCmdHead + kLabAuthLen || n > kLabMaxLen) return std::nullopt;
  if (b[0] != kLabMagic || b[1] != kLabVersion || getBe32(b + 2) != my_hash) return std::nullopt;
  const int a = argLen(b[11]);
  if (a >= 0 && n != kCmdHead + a + kLabAuthLen) return std::nullopt;
  if (!authOk(key, b, n)) return std::nullopt;
  LabCommand c;
  c.dst = my_hash; c.tag = getBe32(b + 6); c.seq = b[10]; c.op = b[11];
  if (c.op == (uint8_t)LabOp::SetTx) c.tx_dbm = (int8_t)b[12];
  if (c.op == (uint8_t)LabOp::SetRadio) c.radio = getRadio(b + 12);
  return c;
}

size_t buildResponse(const LabResponse& r, const char* key, uint8_t* out, size_t cap) {
  if (cap < kLabRespLen) return 0;
  out[0] = kLabMagic; out[1] = kLabVersion | kLabRespBit; putBe32(out + 2, r.src);
  putBe32(out + 6, r.tag); out[10] = r.seq; out[11] = (uint8_t)r.status; putRadio(out + 12, r.radio);
  out[19] = (uint8_t)r.tx_dbm; out[20] = (uint8_t)r.tx_ceiling;
  out[21] = (uint8_t)r.rssi_cmd; out[22] = (uint8_t)r.snr_cmd_q;
  return seal(key, out, kLabRespLen - kLabAuthLen);
}

std::optional<LabResponse> parseResponse(const uint8_t* b, size_t n, uint32_t src_hash, const char* key) {
  if (n != kLabRespLen || b[0] != kLabMagic || b[1] != (kLabVersion | kLabRespBit)) return std::nullopt;
  if (getBe32(b + 2) != src_hash || b[11] > (uint8_t)LabStatus::BadOp) return std::nullopt;
  if (!authOk(key, b, n)) return std::nullopt;
  LabResponse r;
  r.src = src_hash; r.tag = getBe32(b + 6); r.seq = b[10]; r.status = (LabStatus)b[11]; r.radio = getRadio(b + 12);
  r.tx_dbm = (int8_t)b[19]; r.tx_ceiling = (int8_t)b[20];
  r.rssi_cmd = (int8_t)b[21]; r.snr_cmd_q = (int8_t)b[22];
  return r;
}

}  // namespace tracecore
