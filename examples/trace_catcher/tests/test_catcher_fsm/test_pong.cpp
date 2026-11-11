// PONG …: comandos LAB catcher -> ponger por radio (CLAUDE.md §12).
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Fakes.h"
#include "tracecore/Airtime.h"
#include "tracecore/LabFrame.h"

using namespace tracecore;
using fakes::Rig;

namespace {

constexpr uint32_t kPonger = 0xA1B2C3D4;

struct LabRig : Rig {
  LabRig() : Rig(false) {
    cfg.hash = TraceHash(kPonger, 4);
    p = std::make_unique<Catcher>(cfg, rng, out, 0xC3F01A);
  }
  // Manda el comando, confirma la TX y devuelve el comando que vio el "ponger".
  std::optional<LabCommand> send(const char* line, uint32_t t = 0, const char* key = "password") {
    const Action a = p->onCommand(line, t);
    if (a.kind != Action::Transmit) return std::nullopt;
    EXPECT_EQ(a.bytes[0], kLabHeader);
    EXPECT_EQ(a.bytes[1], 0x00);
    sent.assign(a.bytes, a.bytes + a.len);
    EXPECT_EQ(p->onTxDone(t + 200, true).kind, Action::StartRx);
    return parseCommand(sent.data() + 2, sent.size() - 2, kPonger, key);
  }
  Action reply(const LabResponse& r, uint32_t t = 500, const char* key = "password") {
    uint8_t b[2 + kLabMaxLen] = {kLabHeader, 0x00};
    const size_t n = buildResponse(r, key, b + 2, kLabMaxLen);
    return p->onRx(b, n + 2, -100.0f, 5.0f, t);
  }
  std::vector<uint8_t> sent;
};

LabResponse okFor(const LabCommand& c) {
  LabResponse r;
  r.src = kPonger; r.tag = c.tag; r.seq = c.seq; r.radio = LabRadio{915800, 8, 10, 5};
  r.tx_dbm = 2; r.tx_ceiling = 22; r.rssi_cmd = -60; r.snr_cmd_q = 40;
  return r;
}

}  // namespace

TEST(Pong, GetRoundTripEmitsG) {
  LabRig r;
  auto c = r.send("PONG GET");
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->op, (uint8_t)LabOp::Get);
  EXPECT_EQ(r.reply(okFor(*c)).kind, Action::Standby);
  EXPECT_EQ(r.out.lines.back(), "G," + std::to_string(c->seq) + ",ok,915.800,250,10,5,2,22,-60,+10.00");
}

TEST(Pong, TxAndRadioArgsAreEncoded) {
  LabRig r;
  auto t = r.send("PONG TX=-9");
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->tx_dbm, -9);
  r.reply(okFor(*t));
  auto c = r.send("PONG RADIO freq=903.000 bw=125 sf=9 cr=6", 3000);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->op, (uint8_t)LabOp::SetRadio);
  EXPECT_EQ(c->radio.freq_khz, 903000u);
  EXPECT_EQ(c->radio.bw_code, 7);
  EXPECT_EQ(c->radio.sf, 9);
  EXPECT_EQ(c->radio.cr, 6);
}

TEST(Pong, RadioChangesOnlyAfterPongerOk) {  // §12.2.3
  LabRig r;
  auto c = r.send("PONG RADIO freq=903.000 bw=125 sf=9 cr=6");
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(r.p->config().radio.freq_khz, 915800u);   // todavía no
  LabResponse ok = okFor(*c);
  ok.radio = c->radio;
  EXPECT_EQ(r.reply(ok).kind, Action::Reconfigure);
  EXPECT_EQ(r.p->config().radio.freq_khz, 903000u);
  EXPECT_EQ(r.p->config().radio.bw_x100, 12500);
  EXPECT_EQ(r.p->config().radio.sf, 9);

  auto d = r.send("PONG RADIO freq=927.000 bw=125 sf=9 cr=6", 3000);
  ASSERT_TRUE(d.has_value());
  LabResponse range = okFor(*d);
  range.status = LabStatus::Range;
  range.radio = c->radio;
  EXPECT_EQ(r.reply(range, 3500).kind, Action::Standby);   // rango: no cambia
  EXPECT_EQ(r.p->config().radio.freq_khz, 903000u);
}

TEST(Pong, TimeoutAndForeignRepliesAreIgnored) {
  LabRig r;
  auto c = r.send("PONG GET");
  ASSERT_TRUE(c.has_value());
  LabResponse other = okFor(*c);
  other.seq = c->seq + 1;
  r.reply(other);                       // otro seq
  other = okFor(*c);
  other.src = 0x01020304;
  r.reply(other);                       // otro ponger
  other = okFor(*c);
  other.tag ^= 1;
  r.reply(other);                       // mismo seq, otro tag (respuesta de una sesión anterior)
  r.reply(okFor(*c), 600, "otra");      // otra labkey
  EXPECT_EQ(r.out.count("G,"), 0u);
  r.p->tick(60000);
  EXPECT_EQ(r.out.lines.back(), "G," + std::to_string(c->seq) + ",timeout");
}

TEST(Pong, Guards) {
  Rig legacy;                           // hash de 1 byte
  legacy.p->onCommand("PONG GET", 0);
  EXPECT_EQ(legacy.out.lines.back(), "!ERR state nohash4");

  LabRig r;
  r.p->onCommand("PONG TX=40", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR range tx -9-22");
  r.p->onCommand("PONG RADIO freq=903.000 bw=125", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
  r.p->onCommand("PONG FOO", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");

  ASSERT_TRUE(r.send("PONG GET").has_value());
  r.p->onCommand("PING", 300);           // mientras hay un PONG pendiente no se lanza un PING
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
  r.p->onCommand("SET sf=9", 300);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
}

TEST(Pong, LabkeyIsSettableAndUsed) {
  LabRig r;
  r.p->onCommand("SET labkey=secreto", 0);
  EXPECT_EQ(r.out.lines.back(), "#OK labkey=*");
  EXPECT_FALSE(r.send("PONG GET").has_value());          // ya no firma con "password"
  r.p->tick(60000);
  EXPECT_TRUE(r.send("PONG GET", 70000, "secreto").has_value());
  r.p->onCommand("SET labkey=0123456789abcdef", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR range labkey <=15");
}

TEST(Pong, FourByteHashPingsWork) {
  LabRig r;
  fakes::Sim sim(*r.p);
  sim.cmd("PING");
  sim.run(2000);
  ASSERT_EQ(sim.req.size(), 15u);
  EXPECT_EQ(sim.req[10], 0x02);
  EXPECT_EQ(r.out.first("P,").substr(0, 4), "P,1,");
  EXPECT_NE(r.out.first("P,").find(",ok,"), std::string::npos);
  r.p->onCommand("CFG?", 3000);
  EXPECT_NE(r.out.lines.back().find("hash=A1B2C3D4"), std::string::npos);
}

// Canal ocupado al mandar un PONG: se reintenta la misma trama con espera (como los pings); G,busy solo cuando el
// canal lleva ocupado más que el paquete más largo posible.
TEST(Pong, BusyRetriesSameFrameBeforeGivingUp) {
  LabRig r;
  const Action a = r.p->onCommand("PONG GET", 0);
  ASSERT_EQ(a.kind, Action::Transmit);
  const std::vector<uint8_t> first(a.bytes, a.bytes + a.len);
  EXPECT_EQ(r.p->onChannelBusy(1).kind, Action::None);       // 1.er busy: espera
  EXPECT_EQ(r.p->tick(2).kind, Action::None);                 // aún no toca
  Action b = r.p->tick(400);                                  // backoff ≤ 360 ms
  ASSERT_EQ(b.kind, Action::Transmit);
  EXPECT_EQ(std::vector<uint8_t>(b.bytes, b.bytes + b.len), first);   // misma trama, mismo seq
  EXPECT_EQ(r.p->onChannelBusy(401).kind, Action::None);     // 2.º busy: espera
  b = r.p->tick(800);
  ASSERT_EQ(b.kind, Action::Transmit);
  r.p->onChannelBusy(801);                                    // 3.º: antes se rendía aquí
  EXPECT_TRUE(r.out.lines.empty() || r.out.lines.back() != "G,1,busy");   // back() con el vector vacío reventaba
  const uint32_t limit = 1 + (uint32_t)std::ceil(toaMs(r.p->config().radio, 255));
  b = r.p->tick(limit);
  ASSERT_EQ(b.kind, Action::Transmit);
  r.p->onChannelBusy(limit);
  EXPECT_EQ(r.out.lines.back(), "G,1,busy");
}

// Ciclo de TX agotado: el PONG espera (#WARN duty_wait) y sale solo cuando el DutyLimiter lo permite; no se rechaza.
TEST(Pong, DutyExhaustedWaitsInsteadOfRejecting) {
  LabRig r;
  uint32_t t = 0;
  for (int i = 0; i < 200; i++) {
    const Action a = r.p->onCommand("PONG GET", t);
    if (a.kind != Action::Transmit) break;
    r.p->onTxDone(t + 300, true);
    t += 300 + 1300;   // timeout auto del LAB (~1,2 s): un comando tras otro, sin respuesta
    r.p->tick(t);      // G,<seq>,timeout
  }
  EXPECT_EQ(r.out.lines.back().rfind("#WARN duty_wait ", 0), 0u);
  const uint32_t wait = (uint32_t)std::stoul(r.out.lines.back().substr(16));
  EXPECT_EQ(r.p->tick(t + wait - 1).kind, Action::None);
  EXPECT_EQ(r.p->tick(t + wait).kind, Action::Transmit);
}
