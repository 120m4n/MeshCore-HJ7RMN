#include "tracecore/Catcher.h"

#include <cstdarg>
#include <cstdio>

#include "tracecore/LineCodec.h"

#ifndef TRACE_RADIO_NAME
#define TRACE_RADIO_NAME "SX1262"
#endif

namespace tracecore {

Catcher::Catcher(const RadioConfig& cfg, LineSink& out, uint32_t device_id)
    : cfg_(cfg), out_(out), id_(device_id & 0xFFFFFF) {}

void Catcher::say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf_, sizeof buf_, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  out_.line(buf_, (size_t)n >= sizeof buf_ ? sizeof buf_ - 1 : (size_t)n);
}

void Catcher::emitBoot() {
  say("#HELLO fw=%s proto=%d role=catcher radio=%s id=%06X", kFwVersion, kProto, TRACE_RADIO_NAME, (unsigned)id_);
  out_.line(buf_, formatCfg(buf_, sizeof buf_, cfg_));
}

Action Catcher::onCommand(std::string_view line) {
  const Command c = parseCommand(line);
  switch (c.kind) {
    case Command::Kind::Empty: return {};
    case Command::Kind::Hello:
      say("#HELLO fw=%s proto=%d role=catcher radio=%s id=%06X", kFwVersion, kProto, TRACE_RADIO_NAME, (unsigned)id_);
      return {};
    case Command::Kind::CfgQuery:
      out_.line(buf_, formatCfg(buf_, sizeof buf_, cfg_));
      return {};
    case Command::Kind::Set: {
      const SetOutcome o = applySet(cfg_, c.key, c.val);
      switch (o.result) {
        case SetResult::Unknown: say("!ERR unknown %s", c.key); return {};
        case SetResult::Range: say("!ERR range %s", o.detail); return {};
        case SetResult::Syntax: say("!ERR syntax"); return {};
        case SetResult::Ok: break;
      }
      say("#OK %s=%s", c.key, c.val);
      return Action{Action::Reconfigure, 0};
    }
    case Command::Kind::Meas:
      if (c.n < kMeasMin || c.n > kMeasMax) { say("!ERR range meas %u-%u", (unsigned)kMeasMin, (unsigned)kMeasMax); return {}; }
      return Action{Action::Measure, (uint16_t)c.n};
    case Command::Kind::Syntax: say("!ERR syntax"); return {};
    case Command::Kind::Unknown: say("!ERR unknown"); return {};
  }
  return {};
}

void Catcher::emitMeas(const RssiStats& r) { out_.line(buf_, formatMeas(buf_, sizeof buf_, r)); }

}  // namespace tracecore
