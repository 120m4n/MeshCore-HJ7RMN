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

const char* statusText(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Timeout: return "timeout";
    case Status::Busy: return "busy";
    default: return "txerr";
  }
}

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

size_t formatPing(char* out, size_t cap, const PingResult& r) {
  W w{out, cap};
  if (cap) out[0] = 0;
  char tag[9];
  std::snprintf(tag, sizeof tag, "%08X", (unsigned)r.tag);
  w.str("P,"); w.u(r.seq); w.ch(','); w.str(tag); w.ch(','); w.str(statusText(r.status)); w.ch(',');
  if (r.status == Status::Ok) {
    w.u(r.rtt_ms); w.ch(',');
    w.fx(r.snr_up_c, 2, true); w.ch(',');
    w.fx(r.snr_dn_c, 2, true); w.ch(',');
    w.fx(r.rssi_dn_d, 1, true);
  } else {
    w.str(",,,");  // con las comas de alrededor: rtt, snr_up, snr_dn y rssi vacíos
  }
  w.ch(','); w.u(r.n_resp);
  return w.n;
}

size_t formatSummary(char* out, size_t cap, const BurstSummary& s) {
  W w{out, cap};
  if (cap) out[0] = 0;
  w.str("S,"); w.u(s.n); w.ch(','); w.u(s.ok); w.ch(',');
  const uint32_t denom = (uint32_t)s.ok + s.timeouts;
  if (denom) w.u((100u * s.timeouts + denom / 2) / denom);
  w.ch(',');
  if (s.has_median) {
    w.fx(s.med_up_c, 2, true); w.ch(',');
    w.fx(s.med_dn_c, 2, true); w.ch(',');
    w.fx(s.med_rssi_d, 1, true);
  } else {
    w.str(",,");  // tres campos vacíos: up, dn, rssi
  }
  w.ch(','); w.u(s.busy);
  return w.n;
}

size_t formatRaw(char* out, size_t cap, uint32_t t_ms, int32_t rssi_d, int32_t snr_c,
                 const uint8_t* bytes, size_t n) {
  W w{out, cap};
  if (cap) out[0] = 0;
  w.str("R,"); w.u(t_ms); w.ch(',');
  w.fx(rssi_d, 1, true); w.ch(',');
  w.fx(snr_c, 2, true); w.ch(',');
  static const char* hex = "0123456789ABCDEF";
  const size_t m = n > kMaxRawBytes ? kMaxRawBytes : n;
  for (size_t i = 0; i < m; i++) { w.ch(hex[bytes[i] >> 4]); w.ch(hex[bytes[i] & 15]); }
  if (n > m) w.str("..");
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
  if (ieq(word, "SAVE")) { c.kind = Command::Kind::Save; return c; }
  if (ieq(word, "GO")) { c.kind = Command::Kind::Go; return c; }
  if (ieq(word, "STOP")) { c.kind = Command::Kind::Stop; return c; }
  if (ieq(word, "PING")) {
    c.kind = Command::Kind::Ping;
    if (rest.empty()) return c;
    uint32_t n = 0;
    for (char ch : rest) {
      if (ch < '0' || ch > '9' || (n = n * 10 + (ch - '0')) > 9999) { c.kind = Command::Kind::Syntax; return c; }
    }
    c.n = (uint16_t)n;
    c.has_n = true;
    return c;
  }
  if (ieq(word, "MON")) {
    if (ieq(rest, "on")) { c.kind = Command::Kind::Mon; c.on = true; return c; }
    if (ieq(rest, "off")) { c.kind = Command::Kind::Mon; c.on = false; return c; }
    c.kind = Command::Kind::Syntax;
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
