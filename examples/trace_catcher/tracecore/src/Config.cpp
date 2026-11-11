#include "tracecore/Config.h"

#include <cctype>
#include <cstdio>

#include "tracecore/Text.h"

namespace tracecore {
namespace {

struct BwEntry { uint16_t x100; const char* text; };
constexpr BwEntry kBw[] = {{780, "7.8"},    {1040, "10.4"},  {1560, "15.6"},  {2080, "20.8"},
                           {3125, "31.25"}, {4170, "41.7"},  {6250, "62.5"},  {12500, "125"},
                           {25000, "250"},  {50000, "500"}};

constexpr uint32_t kFreqMin = 902000, kFreqMax = 928000;

// "915.800" -> 915800 (kHz). Rechaza más de 3 decimales.
bool parseKhz(std::string_view s, int32_t& out) {
  size_t i = 0;
  int32_t whole = 0, frac = 0;
  int fd = 0;
  for (; i < s.size() && std::isdigit((unsigned char)s[i]); i++) {
    whole = whole * 10 + (s[i] - '0');
    if (whole > 100000) return false;
  }
  if (i == 0) return false;
  if (i < s.size() && s[i] == '.') {
    for (i++; i < s.size() && std::isdigit((unsigned char)s[i]); i++) {
      if (fd >= 3) return false;
      frac = frac * 10 + (s[i] - '0');
      fd++;
    }
  }
  if (i != s.size()) return false;
  for (; fd < 3; fd++) frac *= 10;
  out = whole * 1000 + frac;
  return true;
}

}  // namespace

const char* bwText(uint16_t x100) {
  for (const auto& e : kBw)
    if (e.x100 == x100) return e.text;
  return nullptr;
}

SetOutcome applySet(RadioConfig& c, std::string_view key, std::string_view val) {
  if (!ieq(key, "freq")) return {SetResult::Unknown, nullptr};
  int32_t v = 0;
  if (!parseKhz(val, v)) return {SetResult::Syntax, nullptr};
  if (v < (int32_t)kFreqMin || v > (int32_t)kFreqMax) return {SetResult::Range, "freq 902.000-928.000"};
  c.freq_khz = v;
  return {SetResult::Ok, nullptr};
}

size_t formatCfg(char* out, size_t cap, const RadioConfig& c) {
  const int n = std::snprintf(out, cap, "#CFG freq=%u.%03u bw=%s sf=%u cr=%u", (unsigned)(c.freq_khz / 1000),
                              (unsigned)(c.freq_khz % 1000), bwText(c.bw_x100) ? bwText(c.bw_x100) : "?", c.sf, c.cr);
  return n < 0 ? 0 : ((size_t)n >= cap ? cap - 1 : (size_t)n);
}

}  // namespace tracecore
