#pragma once
#include <memory>
#include <string>
#include <vector>

#include "tracecore/Pinger.h"
#include "tracecore/TraceFrame.h"

namespace fakes {

struct FakeRng : tracecore::Rng {
  std::vector<uint32_t> values;  // se repiten en ciclo; vacío = siempre 0x11111111
  size_t i = 0;
  uint32_t next() override { return values.empty() ? 0x11111111u : values[i++ % values.size()]; }
};

struct CapturingSink : tracecore::LineSink {
  std::vector<std::string> lines;
  void line(const char* s, size_t n) override { lines.emplace_back(s, n); }
  size_t count(const std::string& prefix) const {
    size_t c = 0;
    for (const auto& l : lines) if (l.rfind(prefix, 0) == 0) c++;
    return c;
  }
  std::string first(const std::string& prefix) const {
    for (const auto& l : lines) if (l.rfind(prefix, 0) == 0) return l;
    return "";
  }
};

// Eco válido para la petición `req` (12 B) con SNR de subida en cuartos de dB.
inline std::vector<uint8_t> echoFor(const uint8_t* req, int8_t snr_q) {
  std::vector<uint8_t> e = {0x26, 0x01, (uint8_t)snr_q};
  e.insert(e.end(), req + 2, req + 12);  // tag(4) auth(4) flags(1) route(1)
  return e;
}

struct Rig {
  FakeRng rng;
  CapturingSink out;
  tracecore::PingerConfig cfg;
  std::unique_ptr<tracecore::Pinger> p;
  explicit Rig(bool with_hash = true) {
    if (with_hash) cfg.hash = 0xA1;
    p = std::make_unique<tracecore::Pinger>(cfg, rng, out, 0xC3F01A);
  }
};

// Simula el cascarón: ToA fijo, RTT fijo, ecos opcionales. Avanza de 1 ms en 1 ms.
struct Sim {
  tracecore::Pinger& p;
  uint32_t t = 0;
  uint32_t toa = 177, rtt = 300;
  bool reply = true;
  int8_t snr_q = -25;
  std::vector<uint32_t> tx_starts;
  std::vector<uint8_t> req;
  bool txdone_due = false, echo_due = false;
  uint32_t txdone_at = 0, echo_at = 0;

  explicit Sim(tracecore::Pinger& pp) : p(pp) {}
  void handle(const tracecore::Action& a) {
    if (a.kind != tracecore::Action::Transmit) return;
    tx_starts.push_back(t);
    req.assign(a.bytes, a.bytes + a.len);
    txdone_at = t + toa; txdone_due = true;
    if (reply) { echo_at = txdone_at + rtt; echo_due = true; }
  }
  void cmd(const char* line) { handle(p.onCommand(line, t)); }
  void run(uint32_t until) {
    for (; t <= until; ++t) {
      if (txdone_due && t == txdone_at) { txdone_due = false; handle(p.onTxDone(t, true)); }
      if (echo_due && t == echo_at) {
        echo_due = false;
        auto e = echoFor(req.data(), snr_q);
        p.onRx(e.data(), e.size(), -104.0f, -1.5f, t);
      }
      handle(p.tick(t));
    }
  }
};

}  // namespace fakes
