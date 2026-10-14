#include "Hex.h"

namespace tracecore_test {

std::string toHex(const uint8_t* b, size_t n) {
  static const char* d = "0123456789ABCDEF";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
  return s;
}

static int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool fromHex(const std::string& s, std::vector<uint8_t>& out) {
  out.clear();
  if (s.size() % 2) return false;
  for (size_t i = 0; i < s.size(); i += 2) {
    const int hi = nibble(s[i]), lo = nibble(s[i + 1]);
    if (hi < 0 || lo < 0) { out.clear(); return false; }
    out.push_back((uint8_t)(hi << 4 | lo));
  }
  return true;
}

}  // namespace tracecore_test
