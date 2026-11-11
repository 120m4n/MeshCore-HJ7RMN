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
constexpr uint16_t kMaxWaitS = 120;
constexpr uint8_t kNoEchoStop = 3;   // PING n: tras 3 timeouts seguidos no se sigue (algo está mal: antena, radio, ponger)
}  // namespace

Catcher::Catcher(const CatcherConfig& cfg, Rng& rng, LineSink& out, uint32_t device_id)
    : cfg_(cfg), rng_(rng), out_(out), id_(device_id & 0xFFFFFF) {}
// LAB v2: cada comando lleva un tag aleatorio de 4 bytes (genTag, como el TRACE). Así ninguna trama es idéntica a
// otra aunque el catcher se reinicie y repita seq: la tabla wasSeen de MeshCore en el ponger solo tira duplicados reales.

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
      case Command::Kind::Ping: case Command::Kind::Go: case Command::Kind::Pong: case Command::Kind::Rf:
      case Command::Kind::Mon: case Command::Kind::Last: case Command::Kind::Save:
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
    case Command::Kind::Save: return act(Action::Save);
    case Command::Kind::Rf: return doRf(c.on);
    case Command::Kind::Mon: if (rf_off_ && c.on) { say("!ERR rfoff"); return {}; } return doMon(c.on);
    case Command::Kind::Stop: return doStop();
    case Command::Kind::Last: doLast(); return {};
    case Command::Kind::Go:
      if (rf_off_) { say("!ERR rfoff"); return {}; }
      return startRun(0, false, true, now);
    case Command::Kind::Pong:
      if (rf_off_) { say("!ERR rfoff"); return {}; }
      return doPong(c.arg, now);
    case Command::Kind::Ping:
      if (rf_off_) { say("!ERR rfoff"); return {}; }
      if (c.has_n && (c.n < 1 || c.n > kMaxBurst)) { say("!ERR range ping 1-50"); return {}; }
      if (c.wait_s > kMaxWaitS) { say("!ERR range wait 0-%u", (unsigned)kMaxWaitS); return {}; }
      return startRun(c.has_n ? c.n : 1, c.has_n, false, now, c.wait_s);
  }
  return {};
}

Action Catcher::doSet(const char* key, const char* val) {
  if (ieq(key, "labkey")) {   // no se persiste ni se muestra en #CFG (CLAUDE.md §12.3)
    const size_t n = std::strlen(val);
    if (n >= sizeof labkey_) { say("!ERR range labkey <=15"); return {}; }
    std::memcpy(labkey_, val, n + 1);
    say("#OK labkey=*");
    return {};
  }
  const bool radio = isRadioKey(key);
  if (radio && (inFlight() || state_ == State::LabTx || state_ == State::LabWait)) { say("!ERR state busy"); return {}; }
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

Action Catcher::doMon(bool on) {
  if (on) {
    if (state_ != State::Idle && state_ != State::Mon) { say("!ERR state busy"); return {}; }
    state_ = State::Mon;
    say("#OK mon=on");
    return act(Action::StartRx);
  }
  say("#OK mon=off");
  if (state_ != State::Mon) return {};
  state_ = State::Idle;
  return act(Action::Standby);
}

Action Catcher::doRf(bool on) {
  if (on) {
    const bool was_off = rf_off_;
    rf_off_ = false;
    say("#OK rf=on");
    // La radio quedó en standby con su configuración: basta con volver a RX (el loop lo re-arma).
    return was_off ? act(Action::StartRx) : Action{};
  }
  if (inFlight() || state_ == State::LabTx || state_ == State::LabWait) { say("!ERR state busy"); return {}; }
  state_ = State::Idle;   // corta MON si estaba activo
  rf_off_ = true;
  say("#OK rf=off");
  return act(Action::RadioOff);
}

Action Catcher::doMode(std::string_view arg) {
  const bool pinger = ieq(arg, "pinger");
  if (!pinger && !ieq(arg, "catcher")) { say("!ERR syntax"); return {}; }
  if ((pinger ? Mode::Pinger : Mode::Catcher) == mode_) { say("#OK mode=%s", pinger ? "pinger" : "catcher"); return {}; }
  if (inFlight() || state_ == State::LabTx || state_ == State::LabWait) { say("!ERR state busy"); return {}; }
  const bool was_mon = state_ == State::Mon;
  state_ = State::Idle;   // corta MON
  rf_off_ = false;
  mode_ = pinger ? Mode::Pinger : Mode::Catcher;
  say("#OK mode=%s", pinger ? "pinger" : "catcher");
  return was_mon ? act(Action::Standby) : Action{};
}

Action Catcher::doStop() {
  say("#OK stop");
  return stopRun();
}

Action Catcher::onTransportLost(Origin origin) {
  if (!inFlight() || run_origin_ != origin) return {};
  if (!run_go_ && run_total_ <= 1) return {};   // un ping suelto termina solo
  if (run_detached_) return {};                  // PING n wait=s: el usuario se alejó a propósito
  if (origin == Origin::Ble) say("#WARN ble_lost_stop");
  return stopRun();
}

Action Catcher::stopRun() {
  if (state_ == State::WaitSend) return endRun();
  if (state_ == State::TxPending || state_ == State::RxWait) stop_req_ = true;  // el ping en curso termina
  return {};
}

Action Catcher::startRun(uint16_t count, bool summary, bool go, uint32_t now, uint16_t wait_s) {
  if (state_ == State::Mon) { say("!ERR state mon"); return {}; }
  if (!cfg_.hash) { say("!ERR state nohash"); return {}; }
  if (state_ != State::Idle) { say("!ERR state busy"); return {}; }
  run_total_ = count;
  run_done_ = run_ok_ = run_to_ = run_busy_ = samples_ = 0;
  run_miss_ = 0;
  run_go_ = go;
  run_origin_ = cur_origin_;
  run_summary_ = summary;
  run_detached_ = wait_s > 0;
  if (summary) { last_n_ = 0; last_sum_ = {}; has_last_ = true; last_done_ = false; }
  stop_req_ = false;
  state_ = State::WaitSend;
  next_ms_ = now + wait_s * 1000u;
  new_ping_ = true;
  duty_warned_ = false;
  if (go) say("#OK go");
  if (wait_s) {
    // ETA en s hasta la S, simulando el ciclo de TX con una copia del DutyLimiter: con 30 pings + LAB se pasa del 10 %
    // en 60 s y el catcher para (#WARN duty_wait). Sin esto la app avisaba "vuelve" ~15 s antes de que terminara.
    const uint32_t to = cfg_.timeout_ms ? *cfg_.timeout_ms : timeoutAutoMs(cfg_.radio);
    const uint32_t toa = (uint32_t)std::ceil(toaMs(cfg_.radio, requestLen(*cfg_.hash)));
    DutyLimiter sim = duty_;
    uint32_t t = now + wait_s * 1000u, last = t;
    for (uint16_t i = 0; i < count; i++) {
      last = sim.nextAllowed(t, toa);
      sim.record(last, toa);
      t = last + cfg_.rate_ms;
    }
    const uint32_t eta_ms = last - now + toa + to;
    say("#OK ping n=%u wait=%u eta=%u", (unsigned)count, (unsigned)wait_s, (unsigned)((eta_ms + 999) / 1000));
  }
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
  if (state_ == State::LabWait && reached(now, deadline_)) return labDone("timeout");
  if (state_ == State::LabTx && lab_retry_ && reached(now, lab_retry_at_)) {
    lab_retry_ = false;
    tx_start_ = now;
    Action a = act(Action::Transmit);
    a.bytes = lab_buf_;
    a.len = lab_len_;
    return a;
  }
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
  if (state_ == State::LabTx) {
    if (!ok) return labDone("txerr");
    duty_.record(tx_start_, tx_dur_);
    deadline_ = now + (cfg_.timeout_ms ? *cfg_.timeout_ms : timeoutAutoMs(cfg_.radio));
    state_ = State::LabWait;
    return act(Action::StartRx);
  }
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
  if (state_ == State::LabTx) {
    // Como los pings (y el Dispatcher de MeshCore): canal ocupado => esperar y reintentar la misma trama. Antes se
    // rendía al primer busy y la app se quedaba sin salida (G,busy visto 2 veces seguidas al conectar).
    if (busyTooLong(now)) return labDone("busy");
    lab_retry_at_ = now + ((rng_.next() % 3) + 1) * 120;
    lab_retry_ = true;
    return {};
  }
  if (state_ != State::TxPending) return {};
  if (busyTooLong(now) || stop_req_) return finishWith(Status::Busy, now);
  state_ = State::WaitSend;  // mismo ping (mismo tag), reintento tras backoff como Mesh::getCADFailRetryDelay
  next_ms_ = now + ((rng_.next() % 3) + 1) * 120;
  return {};
}

Action Catcher::onRx(const uint8_t* b, size_t n, float rssi, float snr, uint32_t now) {
  const int32_t rssi_d = std::lround(rssi * 10.0f), snr_c = std::lround(snr * 100.0f);
  if (state_ == State::Mon) { sayRaw(b, n, rssi_d, snr_c, now); return {}; }
  if (state_ == State::LabWait) {
    if (n < 2 || b[0] != kLabHeader || b[1] != 0x00) return {};
    const auto r = parseResponse(b + 2, n - 2, cfg_.hash->v, labkey_);
    if (!r || r->seq != lab_cmd_.seq || r->tag != lab_cmd_.tag) return {};
    if (cfg_.raw) sayRaw(b, n, rssi_d, snr_c, now);
    emitLab(*r);
    state_ = State::Idle;
    // §12.2.3: el catcher cambia de radio solo al recibir el ok del ponger.
    if (r->status == LabStatus::Ok && (lab_cmd_.op == (uint8_t)LabOp::SetRadio || lab_cmd_.op == (uint8_t)LabOp::Reset)) {
      cfg_.radio.freq_khz = r->radio.freq_khz;
      cfg_.radio.bw_x100 = (uint16_t)(kLabBwHz[r->radio.bw_code < 10 ? r->radio.bw_code : 8] / 10);
      cfg_.radio.sf = r->radio.sf;
      cfg_.radio.cr = r->radio.cr;
      return act(Action::Reconfigure);
    }
    return act(Action::Standby);
  }
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
  if (run_summary_ && last_n_ < kMaxBurst) last_[last_n_++] = r;
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
    last_sum_ = s;
  }
  if (run_summary_) last_done_ = true;   // también si se paró en la espera (n=0)
  state_ = State::Idle;
  run_go_ = false;
  run_detached_ = false;
  stop_req_ = false;
  return act(Action::Standby);
}

Action Catcher::doPong(std::string_view arg, uint32_t now) {
  if (!cfg_.hash || cfg_.hash->len != 4) { say("!ERR state nohash4"); return {}; }   // LAB exige hash de 4 bytes
  if (state_ != State::Idle) { say("!ERR state busy"); return {}; }

  const size_t sp = arg.find(' ');
  const std::string_view word = arg.substr(0, sp);
  std::string_view rest = sp == std::string_view::npos ? std::string_view() : arg.substr(sp + 1);
  LabCommand c;
  c.dst = cfg_.hash->v;
  CatcherConfig tmp = cfg_;   // se reutiliza la validación de SET para TX y RADIO
  if (ieq(word, "GET") && rest.empty()) {
    c.op = (uint8_t)LabOp::Get;
  } else if (ieq(word, "RESET") && rest.empty()) {
    c.op = (uint8_t)LabOp::Reset;
  } else if (word.size() > 3 && ieq(word.substr(0, 3), "TX=") && rest.empty()) {
    const SetOutcome o = applySet(tmp, "txp", word.substr(3));
    if (o.result != SetResult::Ok) { say(o.result == SetResult::Range ? "!ERR range tx -9-22" : "!ERR syntax"); return {}; }
    c.op = (uint8_t)LabOp::SetTx;
    c.tx_dbm = tmp.radio.txp_dbm;
  } else if (ieq(word, "RADIO")) {
    uint8_t seen = 0;
    static const char* const keys[] = {"freq", "bw", "sf", "cr"};
    while (!rest.empty()) {
      const size_t e = rest.find(' ');
      const std::string_view kv = rest.substr(0, e);
      rest = e == std::string_view::npos ? std::string_view() : rest.substr(e + 1);
      if (kv.empty()) continue;
      const size_t eq = kv.find('=');
      int k = -1;
      for (int i = 0; i < 4 && eq != std::string_view::npos; i++)
        if (ieq(kv.substr(0, eq), keys[i])) k = i;
      if (k < 0) { say("!ERR syntax"); return {}; }
      const SetOutcome o = applySet(tmp, keys[k], kv.substr(eq + 1));
      if (o.result == SetResult::Range) { say("!ERR range %s", o.detail); return {}; }
      if (o.result != SetResult::Ok) { say("!ERR syntax"); return {}; }
      seen |= 1 << k;
    }
    if (seen != 0x0F) { say("!ERR syntax"); return {}; }   // freq, bw, sf y cr, los cuatro
    c.op = (uint8_t)LabOp::SetRadio;
    c.radio.freq_khz = tmp.radio.freq_khz;
    c.radio.bw_code = (uint8_t)labBwCode(tmp.radio.bwKhz());
    c.radio.sf = tmp.radio.sf;
    c.radio.cr = tmp.radio.cr;
  } else {
    say("!ERR syntax");
    return {};
  }

  c.seq = ++lab_seq_;
  c.tag = genTag();
  lab_buf_[0] = kLabHeader;
  lab_buf_[1] = 0x00;   // path_len
  lab_len_ = (uint8_t)(2 + buildCommand(c, labkey_, lab_buf_ + 2, kLabMaxLen));
  tx_dur_ = (uint32_t)std::ceil(toaMs(cfg_.radio, lab_len_));
  const uint32_t ok_at = duty_.nextAllowed(now, tx_dur_);
  lab_cmd_ = c;
  cad_tries_ = 0;
  state_ = State::LabTx;
  if (ok_at != now) {
    // Ciclo de TX agotado (p. ej. justo tras 30 pings): se espera como los pings, no se rechaza. Antes daba
    // !ERR state duty y la sincronización de la app entre fases fallaba (visto 2026-11-05). tick() lo manda.
    say("#WARN duty_wait %u", (unsigned)(ok_at - now));
    lab_retry_ = true;
    lab_retry_at_ = ok_at;
    return {};
  }
  lab_retry_ = false;
  tx_start_ = now;
  Action a = act(Action::Transmit);
  a.bytes = lab_buf_;
  a.len = lab_len_;
  return a;
}

void Catcher::doLast() {
  if (!has_last_) { say("!ERR state nolast"); return; }
  if (!last_done_) { say("!ERR state busy"); return; }   // la ráfaga sigue: la app espera a la ETA
  say("#OK last n=%u", (unsigned)last_n_);
  for (uint16_t i = 0; i < last_n_; i++) out_.line(buf_, formatPing(buf_, sizeof buf_, last_[i]));
  out_.line(buf_, formatSummary(buf_, sizeof buf_, last_sum_));
}

Action Catcher::labDone(const char* status) {
  say("G,%u,%s", (unsigned)lab_cmd_.seq, status);
  state_ = State::Idle;
  return act(Action::Standby);
}

void Catcher::emitLab(const LabResponse& r) {
  static const char* const st[] = {"ok", "range", "badop"};
  const char* bw = r.radio.bw_code < 10 ? bwText((uint16_t)(kLabBwHz[r.radio.bw_code] / 10)) : nullptr;
  char snr[12];
  fmtFixed(snr, sizeof snr, r.snr_cmd_q * 25, 2, true);
  say("G,%u,%s,%u.%03u,%s,%u,%u,%d,%d,%d,%s", (unsigned)r.seq, st[(int)r.status],
      (unsigned)(r.radio.freq_khz / 1000), (unsigned)(r.radio.freq_khz % 1000), bw ? bw : "?",
      r.radio.sf, r.radio.cr, r.tx_dbm, r.tx_ceiling, r.rssi_cmd, snr);
}

void Catcher::onSaveDone(bool ok) {
  if (ok) say("#OK saved");
  else say("!ERR state flash");
}

}  // namespace tracecore
