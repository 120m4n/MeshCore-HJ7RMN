#include <cmath>
#include "tracecore/LineCodec.h"

#include <cstdio>
#include <cstring>

#include "tracecore/Text.h"

namespace tracecore {
namespace {

// Escritor con tope: nunca desborda y siempre deja '\0'.
struct W {
  char* b;
  size_t cap;
  size_t n = 0;
  void ch(char c) { if (n + 1 < cap) { b[n++] = c; b[n] = 0; } }
  void str(const char* s) { while (*s) ch(*s++); }
  void u(uint32_t v) { char t[12]; std::snprintf(t, sizeof t, "%u", (unsigned)v); str(t); }
  void fx(int32_t v, int dec, bool sign) {
    char t[20];
    fmtFixed(t, sizeof t, v, dec, sign);
    str(t);
  }
};

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  return s;
}

}  // namespace

size_t fmtFixed(char* out, size_t cap, int32_t v, int decimals, bool force_sign) {
  const uint32_t a = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
  uint32_t scale = 1;
  for (int i = 0; i < decimals; i++) scale *= 10;
  const char* sg = v < 0 ? "-" : (force_sign ? "+" : "");
  int n = decimals ? std::snprintf(out, cap, "%s%u.%0*u", sg, (unsigned)(a / scale), decimals, (unsigned)(a % scale))
                   : std::snprintf(out, cap, "%s%u", sg, (unsigned)a);
  return n < 0 ? 0 : ((size_t)n >= cap ? cap - 1 : (size_t)n);
}

size_t formatMeas(char* out, size_t cap, const RssiStats& r) {
  W w{out, cap};
  if (cap) out[0] = 0;
  auto c = [](float v, float k) { return (int32_t)std::lround(v * k); };
  w.str("M,"); w.u(r.n); w.ch(','); w.u(r.kept); w.ch(',');
  w.fx(c(r.mean, 100), 2, false); w.ch(',');
  w.fx(c(r.sd, 100), 2, false); w.ch(',');
  w.fx(c(r.median, 10), 1, false); w.ch(',');
  w.fx(c(r.min, 10), 1, false); w.ch(',');
  w.fx(c(r.max, 10), 1, false);
  return w.n;
}

Command parseCommand(std::string_view line) {
  Command c;
  line = trim(line);
  if (line.empty()) return c;
  const size_t sp = line.find(' ');
  const std::string_view word = line.substr(0, sp);
  const std::string_view rest = sp == std::string_view::npos ? std::string_view() : trim(line.substr(sp + 1));

  if (ieq(word, "HELLO")) { c.kind = Command::Kind::Hello; return c; }
  if (ieq(word, "CFG?")) { c.kind = Command::Kind::CfgQuery; return c; }
  if (ieq(word, "MEAS")) {   // MEAS [n]
    c.kind = Command::Kind::Meas;
    if (rest.empty()) return c;
    uint32_t n = 0;
    for (char ch : rest) {
      if (ch < '0' || ch > '9' || (n = n * 10 + (ch - '0')) > 100000) { c.kind = Command::Kind::Syntax; return c; }
    }
    c.n = n;
    return c;
  }
  if (ieq(word, "SET")) {
    const size_t eq = rest.find('=');
    const std::string_view k = eq == std::string_view::npos ? std::string_view() : trim(rest.substr(0, eq));
    const std::string_view v = eq == std::string_view::npos ? std::string_view() : trim(rest.substr(eq + 1));
    if (k.empty() || v.empty() || k.size() >= sizeof c.key || v.size() >= sizeof c.val) {
      c.kind = Command::Kind::Syntax;
      return c;
    }
    c.kind = Command::Kind::Set;
    std::memcpy(c.key, k.data(), k.size());
    std::memcpy(c.val, v.data(), v.size());
    return c;
  }
  c.kind = Command::Kind::Unknown;
  return c;
}

}  // namespace tracecore
