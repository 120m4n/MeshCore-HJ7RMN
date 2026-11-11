#include "tracecore/Catcher.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "tracecore/Airtime.h"
#include "tracecore/LineCodec.h"
#include "tracecore/Text.h"
#include "tracecore/TraceFrame.h"

#ifndef TRACE_RADIO_NAME
#define TRACE_RADIO_NAME "SX1262"
#endif

namespace tracecore {
namespace {
inline bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
constexpr uint8_t kNoEchoStop = 3;   // PING n: tras 3 timeouts seguidos no se sigue (algo está mal: antena, radio, repetidor)
}  // namespace

Catcher::Catcher(const CatcherConfig& cfg, Rng& rng, LineSink& out, uint32_t device_id)
    : cfg_(cfg), rng_(rng), out_(out), id_(device_id & 0xFFFFFF) {}

void Catcher::say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(buf_, sizeof buf_, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  out_.line(buf_, (size_t)n >= sizeof buf_ ? sizeof buf_ - 1 : (size_t)n);
}

void Catcher::sayRaw(const uint8_t* b, size_t n, int32_t rssi_d, int32_t snr_c, uint32_t now) {
  out_.line(buf_, formatRaw(buf_, sizeof buf_, now, rssi_d, snr_c, b, n));
}

void Catcher::emitBoot(bool cfg_reset) {
  say("#HELLO fw=%s proto=%d role=%s radio=%s id=%06X", kFwVersion, kProto, mode_ == Mode::Pinger ? "pinger" : "catcher", TRACE_RADIO_NAME, (unsigned)id_);
  if (cfg_reset) say("#WARN cfg_reset");
  out_.line(buf_, formatCfg(buf_, sizeof buf_, cfg_));
}

Action Catcher::onCommand(std::string_view line, uint32_t now, Origin origin) {
  cur_origin_ = origin;
  const Command c = parseCommand(line);
  if (mode_ == Mode::Catcher) {   // el catcher no transmite: nada de lo que pueda emitir una trama
    switch (c.kind) {
      case Command::Kind::Ping: case Command::Kind::Go:
        say("!ERR mode");
        return {};
      default: break;
    }
  }
  switch (c.kind) {
    case Command::Kind::Mode: return doMode(c.arg);
    case Command::Kind::Empty: return {};
    case Command::Kind::Hello:
      say("#HELLO fw=%s proto=%d role=%s radio=%s id=%06X", kFwVersion, kProto, mode_ == Mode::Pinger ? "pinger" : "catcher", TRACE_RADIO_NAME, (unsigned)id_);
      return {};
    case Command::Kind::CfgQuery:
      out_.line(buf_, formatCfg(buf_, sizeof buf_, cfg_));
      return {};
    case Command::Kind::Unknown: say("!ERR unknown"); return {};
    case Command::Kind::Syntax: say("!ERR syntax"); return {};
    case Command::Kind::Set: return doSet(c.key, c.val);
    case Command::Kind::Stop: return doStop();
    case Command::Kind::Go:
      return startRun(0, false, true, now);
    case Command::Kind::Ping:
      if (c.has_n && (c.n < 1 || c.n > kMaxBurst)) { say("!ERR range ping 1-50"); return {}; }
      return startRun(c.has_n ? c.n : 1, c.has_n, false, now);
  }
  return {};
}

Action Catcher::doSet(const char* key, const char* val) {
  const bool radio = isRadioKey(key);
  if (radio && inFlight()) { say("!ERR state busy"); return {}; }
  const SetOutcome o = applySet(cfg_, key, val);
  switch (o.result) {
    case SetResult::Unknown: say("!ERR unknown %s", key); return {};
    case SetResult::Range: say("!ERR range %s", o.detail); return {};
    case SetResult::Syntax: say("!ERR syntax"); return {};
    case SetResult::Ok: break;
  }
  say("#OK %s=%s", key, val);
  if (ieq(key, "hash") && cfg_.hash && cfg_.hash->len == 1 && (cfg_.hash->v == 0x00 || cfg_.hash->v == 0xFF))
    say("#WARN hash_suspicious");
  return o.radio_changed ? act(Action::Reconfigure) : Action{};
}

Action Catcher::doMode(std::string_view arg) {
  const bool pinger = ieq(arg, "pinger");
  if (!pinger && !ieq(arg, "catcher")) { say("!ERR syntax"); return {}; }
  if ((pinger ? Mode::Pinger : Mode::Catcher) == mode_) { say("#OK mode=%s", pinger ? "pinger" : "catcher"); return {}; }
  if (inFlight()) { say("!ERR state busy"); return {}; }
  mode_ = pinger ? Mode::Pinger : Mode::Catcher;
  say("#OK mode=%s", pinger ? "pinger" : "catcher");
  return {};
}

Action Catcher::doStop() {
  say("#OK stop");
  return stopRun();
}

Action Catcher::onTransportLost(Origin origin) {
  if (!inFlight() || run_origin_ != origin) return {};
  if (!run_go_ && run_total_ <= 1) return {};   // un ping suelto termina solo
  if (origin == Origin::Ble) say("#WARN ble_lost_stop");
  return stopRun();
}

Action Catcher::stopRun() {
  if (state_ == State::WaitSend) return endRun();
  if (state_ == State::TxPending || state_ == State::RxWait) stop_req_ = true;  // el ping en curso termina
  return {};
}

Action Catcher::startRun(uint16_t count, bool summary, bool go, uint32_t now) {
  if (!cfg_.hash) { say("!ERR state nohash"); return {}; }
  if (state_ != State::Idle) { say("!ERR state busy"); return {}; }
  run_total_ = count;
  run_done_ = run_ok_ = run_to_ = run_busy_ = samples_ = 0;
  run_miss_ = 0;
  run_go_ = go;
  run_origin_ = cur_origin_;
  run_summary_ = summary;
  stop_req_ = false;
  state_ = State::WaitSend;
  next_ms_ = now;
  new_ping_ = true;
  duty_warned_ = false;
  if (go) say("#OK go");
  return tick(now);
}

uint32_t Catcher::genTag() {
  uint32_t t = 0;
  for (int i = 0; i < 8; i++) {
    t = rng_.next();
    if (!has_last_tag_ || t != last_tag_) break;
    t = last_tag_ + 1;  // el RNG repite: forzar un tag distinto
  }
  last_tag_ = t;
  has_last_tag_ = true;
  return t;
}

Action Catcher::tryBegin(uint32_t now) {
  tx_dur_ = (uint32_t)std::ceil(toaMs(cfg_.radio, requestLen(*cfg_.hash)));
  const uint32_t ok_at = duty_.nextAllowed(now, tx_dur_);
  if (ok_at != now) {
    if (!duty_warned_) { say("#WARN duty_wait %u", (unsigned)(ok_at - now)); duty_warned_ = true; }
    next_ms_ = ok_at;
    return {};
  }
  duty_warned_ = false;
  if (new_ping_) {
    pend_.seq = ++seq_;
    pend_.tag = genTag();
    pend_.hash = *cfg_.hash;
    buildRequest(pend_.tag, pend_.hash, tx_buf_);
    cad_tries_ = 0;
    echoes_ = 0;
    ping_start_ = now;
    new_ping_ = false;
  }
  state_ = State::TxPending;
  tx_start_ = now;
  Action a = act(Action::Transmit);
  a.bytes = tx_buf_;
  a.len = requestLen(pend_.hash);
  return a;
}

Action Catcher::tick(uint32_t now) {
  Action a;
  if (state_ == State::RxWait && reached(now, deadline_))
    a = finishWith(echoes_ ? Status::Ok : Status::Timeout, now);
  if (state_ == State::WaitSend && reached(now, next_ms_)) {
    const Action b = tryBegin(now);
    if (b.kind != Action::None) return b;
  }
  return a;
}

Action Catcher::onTxDone(uint32_t now, bool ok) {
  if (state_ != State::TxPending) return {};
  if (!ok) return finishWith(Status::TxErr, now);
  duty_.record(tx_start_, tx_dur_);
  t_tx_done_ = now;
  deadline_ = now + (cfg_.timeout_ms ? *cfg_.timeout_ms : timeoutAutoMs(cfg_.radio));
  state_ = State::RxWait;
  return act(Action::StartRx);
}

// Canal ocupado: se reintenta por TIEMPO, no por intentos. Un falso preámbulo se borra en ~1 preámbulo y un paquete
// real termina dentro del más largo posible (255 B): si sigue ocupado más allá, es interferencia continua => busy.
bool Catcher::busyTooLong(uint32_t now) {
  if (cad_tries_++ == 0) busy_t0_ = now;
  return now - busy_t0_ >= (uint32_t)std::ceil(toaMs(cfg_.radio, 255));
}

Action Catcher::onChannelBusy(uint32_t now) {
  if (state_ != State::TxPending) return {};
  if (busyTooLong(now) || stop_req_) return finishWith(Status::Busy, now);
  state_ = State::WaitSend;  // mismo ping (mismo tag), reintento tras backoff como Mesh::getCADFailRetryDelay
  next_ms_ = now + ((rng_.next() % 3) + 1) * 120;
  return {};
}

Action Catcher::onRx(const uint8_t* b, size_t n, float rssi, float snr, uint32_t now) {
  const int32_t rssi_d = std::lround(rssi * 10.0f), snr_c = std::lround(snr * 100.0f);
  if (state_ == State::RxWait) {
    const auto e = parseEcho(b, n, pend_.hash);
    if (e && e->tag == pend_.tag) {
      if (cfg_.raw) sayRaw(b, n, rssi_d, snr_c, now);
      if (echoes_++ == 0) {
        first_snr_up_q_ = e->snr_up_q;
        first_snr_dn_c_ = snr_c;
        first_rssi_d_ = rssi_d;
        first_rx_ms_ = now;
      }
    }
    return {};
  }
  // Eco tardío (ya no hay tag pendiente): solo se registra con raw=1.
  if (cfg_.raw && cfg_.hash && parseEcho(b, n, *cfg_.hash)) sayRaw(b, n, rssi_d, snr_c, now);
  return {};
}

Action Catcher::finishWith(Status st, uint32_t now) {
  PingResult r;
  r.seq = pend_.seq;
  r.tag = pend_.tag;
  r.status = st;
  r.n_resp = echoes_;
  if (st == Status::Ok) {
    r.rtt_ms = first_rx_ms_ - t_tx_done_;
    r.snr_up_c = first_snr_up_q_ * 25;
    r.snr_dn_c = first_snr_dn_c_;
    r.rssi_dn_d = first_rssi_d_;
  }
  out_.line(buf_, formatPing(buf_, sizeof buf_, r));
  if (echoes_ > 1) say("#WARN hash_collision");

  run_done_++;
  if (st == Status::Ok) {
    run_ok_++;
    if (samples_ < kMaxBurst) {
      up_c_[samples_] = r.snr_up_c;
      dn_c_[samples_] = r.snr_dn_c;
      rssi_d_[samples_] = r.rssi_dn_d;
      samples_++;
    }
  } else if (st == Status::Timeout) {
    run_to_++;
  } else {
    run_busy_++;
  }

  run_miss_ = st == Status::Timeout ? run_miss_ + 1 : 0;
  if (run_summary_ && run_miss_ >= kNoEchoStop && run_done_ < run_total_) {
    say("#WARN noecho %u", (unsigned)run_miss_);   // sin eco: no se gasta la ráfaga entera
    return endRun();
  }
  if (stop_req_ || (!run_go_ && run_done_ >= run_total_)) return endRun();
  state_ = State::WaitSend;  // siguiente inicio = max(inicio + rate, ahora)
  new_ping_ = true;
  const uint32_t at = ping_start_ + cfg_.rate_ms;
  next_ms_ = reached(now, at) ? now : at;
  return {};
}

Action Catcher::endRun() {
  if (run_summary_ && run_done_ > 0) {
    BurstSummary s;
    s.n = run_done_;
    s.ok = run_ok_;
    s.timeouts = run_to_;
    s.busy = run_busy_;
    s.has_median = samples_ > 0 && median(up_c_, samples_, s.med_up_c) &&
                   median(dn_c_, samples_, s.med_dn_c) && median(rssi_d_, samples_, s.med_rssi_d);
    out_.line(buf_, formatSummary(buf_, sizeof buf_, s));
  }
  state_ = State::Idle;
  run_go_ = false;
  stop_req_ = false;
  return act(Action::Standby);
}

}  // namespace tracecore
