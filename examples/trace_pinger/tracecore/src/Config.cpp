#include "tracecore/Config.h"

#include <cstdio>

#include "tracecore/Text.h"

namespace tracecore {
namespace {

struct BwEntry { uint16_t x100; const char* text; };
constexpr BwEntry kBw[] = {{780, "7.8"},    {1040, "10.4"},  {1560, "15.6"},  {2080, "20.8"},
                           {3125, "31.25"}, {4170, "41.7"},  {6250, "62.5"},  {12500, "125"},
                           {25000, "250"},  {50000, "500"}};

constexpr uint32_t kFreqMin = 902000, kFreqMax = 928000;
constexpr int kTxpMin = -9, kTxpMax = 22;
constexpr uint32_t kRateMin = 1000, kRateMax = 60000;
constexpr uint32_t kTimeoutMin = 200, kTimeoutMax = 10000;

const char* bwText(uint16_t x100) {
  for (const auto& e : kBw)
    if (e.x100 == x100) return e.text;
  return nullptr;
}

// "915.800" con digits=3 -> 915800. Rechaza más decimales de los permitidos.
bool parseFixed(std::string_view s, int digits, int32_t& out) {
  size_t i = 0;
  bool neg = false;
  if (i < s.size() && s[i] == '-') { neg = true; i++; }
  const size_t start = i;
  int32_t whole = 0, frac = 0;
  int fd = 0;
  for (; i < s.size() && std::isdigit((unsigned char)s[i]); i++) {
    whole = whole * 10 + (s[i] - '0');
    if (whole > 100000) return false;
  }
  if (i == start) return false;
  if (i < s.size() && s[i] == '.') {
    for (i++; i < s.size() && std::isdigit((unsigned char)s[i]); i++) {
      if (fd >= digits) return false;
      frac = frac * 10 + (s[i] - '0');
      fd++;
    }
  }
  if (i != s.size()) return false;
  int32_t scale = 1;
  for (int k = 0; k < digits; k++) scale *= 10;
  for (; fd < digits; fd++) frac *= 10;
  out = (whole * scale + frac) * (neg ? -1 : 1);
  return true;
}

bool hexNibble(char c, uint8_t& v) {
  if (c >= '0' && c <= '9') { v = c - '0'; return true; }
  if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
  if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
  return false;
}

bool parseHex2(std::string_view s, uint8_t& out) {
  uint8_t hi, lo;
  if (s.size() != 2 || !hexNibble(s[0], hi) || !hexNibble(s[1], lo)) return false;
  out = (hi << 4) | lo;
  return true;
}

uint16_t crc16(const uint8_t* d, size_t n) {  // CRC-16/CCITT-FALSE
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)d[i] << 8;
    for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1);
  }
  return c;
}

void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }

}  // namespace

bool isRadioKey(std::string_view k) {
  return ieq(k, "freq") || ieq(k, "bw") || ieq(k, "sf") || ieq(k, "cr") || ieq(k, "txp");
}

bool isValid(const PingerConfig& c) {
  const auto& r = c.radio;
  return r.freq_khz >= kFreqMin && r.freq_khz <= kFreqMax && bwText(r.bw_x100) && r.sf >= 5 &&
         r.sf <= 12 && r.cr >= 5 && r.cr <= 8 && r.txp_dbm >= kTxpMin && r.txp_dbm <= kTxpMax &&
         c.rate_ms >= kRateMin && c.rate_ms <= kRateMax &&
         (!c.timeout_ms || (*c.timeout_ms >= kTimeoutMin && *c.timeout_ms <= kTimeoutMax));
}

SetOutcome applySet(PingerConfig& c, std::string_view key, std::string_view val) {
  int32_t v = 0;
  if (ieq(key, "freq")) {
    if (!parseFixed(val, 3, v)) return {SetResult::Syntax, nullptr, false};
    if (v < (int32_t)kFreqMin || v > (int32_t)kFreqMax)
      return {SetResult::Range, "freq 902.000-928.000", false};
    c.radio.freq_khz = v;
    return {SetResult::Ok, nullptr, true};
  }
  if (ieq(key, "bw")) {
    if (!parseFixed(val, 2, v)) return {SetResult::Syntax, nullptr, false};
    if (v < 0 || !bwText((uint16_t)v)) return {SetResult::Range, "bw 7.8|10.4|15.6|20.8|31.25|41.7|62.5|125|250|500", false};
    c.radio.bw_x100 = (uint16_t)v;
    return {SetResult::Ok, nullptr, true};
  }
  if (ieq(key, "sf")) {
    if (!parseFixed(val, 0, v)) return {SetResult::Syntax, nullptr, false};
    if (v < 5 || v > 12) return {SetResult::Range, "sf 5-12", false};
    c.radio.sf = (uint8_t)v;
    return {SetResult::Ok, nullptr, true};
  }
  if (ieq(key, "cr")) {
    if (!parseFixed(val, 0, v)) return {SetResult::Syntax, nullptr, false};
    if (v < 5 || v > 8) return {SetResult::Range, "cr 5-8", false};
    c.radio.cr = (uint8_t)v;
    return {SetResult::Ok, nullptr, true};
  }
  if (ieq(key, "txp")) {
    if (!parseFixed(val, 0, v)) return {SetResult::Syntax, nullptr, false};
    if (v < kTxpMin || v > kTxpMax) return {SetResult::Range, "txp -9-22", false};
    c.radio.txp_dbm = (int8_t)v;
    return {SetResult::Ok, nullptr, true};
  }
  if (ieq(key, "hash")) {
    uint8_t h;
    if (!parseHex2(val, h)) return {SetResult::Range, "hash 00-FF", false};
    c.hash = h;
    return {SetResult::Ok, nullptr, false};
  }
  if (ieq(key, "rate")) {
    if (!parseFixed(val, 0, v)) return {SetResult::Syntax, nullptr, false};
    if (v < (int32_t)kRateMin || v > (int32_t)kRateMax) return {SetResult::Range, "rate>=1000", false};
    c.rate_ms = v;
    return {SetResult::Ok, nullptr, false};
  }
  if (ieq(key, "timeout")) {
    if (ieq(val, "auto")) { c.timeout_ms.reset(); return {SetResult::Ok, nullptr, false}; }
    if (!parseFixed(val, 0, v)) return {SetResult::Syntax, nullptr, false};
    if (v < (int32_t)kTimeoutMin || v > (int32_t)kTimeoutMax)
      return {SetResult::Range, "timeout auto|200-10000", false};
    c.timeout_ms = (uint32_t)v;
    return {SetResult::Ok, nullptr, false};
  }
  if (ieq(key, "raw")) {
    if (val != "0" && val != "1") return {SetResult::Range, "raw 0|1", false};
    c.raw = (val == "1");
    return {SetResult::Ok, nullptr, false};
  }
  return {SetResult::Unknown, nullptr, false};
}

size_t formatCfg(char* out, size_t cap, const PingerConfig& c) {
  char hash[3] = "";
  if (c.hash) std::snprintf(hash, sizeof hash, "%02X", *c.hash);
  char to[12] = "auto";
  if (c.timeout_ms) std::snprintf(to, sizeof to, "%u", (unsigned)*c.timeout_ms);
  int n = std::snprintf(out, cap, "#CFG freq=%u.%03u bw=%s sf=%u cr=%u txp=%d hash=%s rate=%u timeout=%s raw=%d",
                        (unsigned)(c.radio.freq_khz / 1000), (unsigned)(c.radio.freq_khz % 1000),
                        bwText(c.radio.bw_x100) ? bwText(c.radio.bw_x100) : "?", c.radio.sf, c.radio.cr,
                        c.radio.txp_dbm, hash, (unsigned)c.rate_ms, to, c.raw ? 1 : 0);
  return n < 0 ? 0 : ((size_t)n >= cap ? cap - 1 : (size_t)n);
}

size_t serialize(const PingerConfig& c, uint8_t out[kCfgBlobSize]) {
  uint8_t* p = out;
  *p++ = 'Y'; *p++ = 'P'; *p++ = 'N'; *p++ = 'G';
  *p++ = 1;  // versión
  put32(p, c.radio.freq_khz); p += 4;
  put16(p, c.radio.bw_x100); p += 2;
  *p++ = c.radio.sf;
  *p++ = c.radio.cr;
  *p++ = (uint8_t)c.radio.txp_dbm;
  *p++ = c.hash ? 1 : 0;
  *p++ = c.hash.value_or(0);
  put32(p, c.rate_ms); p += 4;
  *p++ = c.timeout_ms ? 1 : 0;
  put32(p, c.timeout_ms.value_or(0)); p += 4;
  *p++ = c.raw ? 1 : 0;
  put16(p, crc16(out, (size_t)(p - out))); p += 2;
  return (size_t)(p - out);
}

std::optional<PingerConfig> deserialize(const uint8_t* b, size_t n) {
  if (n != kCfgBlobSize || b[0] != 'Y' || b[1] != 'P' || b[2] != 'N' || b[3] != 'G' || b[4] != 1)
    return std::nullopt;
  if (crc16(b, kCfgBlobSize - 2) != get16(b + kCfgBlobSize - 2)) return std::nullopt;
  PingerConfig c;
  const uint8_t* p = b + 5;
  c.radio.freq_khz = get32(p); p += 4;
  c.radio.bw_x100 = get16(p); p += 2;
  c.radio.sf = *p++;
  c.radio.cr = *p++;
  c.radio.txp_dbm = (int8_t)*p++;
  const bool has_hash = *p++ != 0;
  const uint8_t hash = *p++;
  if (has_hash) c.hash = hash;
  c.rate_ms = get32(p); p += 4;
  const bool has_to = *p++ != 0;
  const uint32_t to = get32(p); p += 4;
  if (has_to) c.timeout_ms = to;
  c.raw = *p++ != 0;
  if (!isValid(c)) return std::nullopt;
  return c;
}

}  // namespace tracecore
