#include <gtest/gtest.h>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

TEST(Burst, FivePingsWithSummary) {
  Rig r;
  Sim s(*r.p);
  s.cmd("PING 5");
  s.run(9000);
  // rate = inicio a inicio; el ping dura 177 + 1207 ms < 1500 ms
  ASSERT_EQ(s.tx_starts.size(), 5u);
  for (size_t i = 0; i < 5; i++) EXPECT_EQ(s.tx_starts[i], i * 1500u);
  EXPECT_EQ(r.out.count("P,"), 5u);
  EXPECT_EQ(r.out.lines.back(), "S,5,5,0,-6.25,-1.50,-104.0,0");
}

TEST(Burst, SinglePingHasNoSummary) {
  Rig r;
  Sim s(*r.p);
  s.cmd("PING");
  s.run(3000);
  EXPECT_EQ(r.out.count("P,"), 1u);
  EXPECT_EQ(r.out.count("S,"), 0u);
}

TEST(Burst, LossAndMediansIgnoreTimeouts) {
  Rig r;
  Sim s(*r.p);
  s.reply = false;
  s.cmd("PING 2");
  s.run(5000);
  EXPECT_EQ(r.out.lines.back(), "S,2,0,100,,,,0");
}

TEST(Burst, StopMidBurstEmitsPartialSummary) {
  Rig r;
  Sim s(*r.p);
  s.cmd("PING 5");
  s.run(3500);  // el 3.er ping (inicio 3000) está esperando su eco
  s.cmd("STOP");
  EXPECT_EQ(r.out.lines.back(), "#OK stop");
  s.run(6000);
  EXPECT_EQ(s.tx_starts.size(), 3u);
  EXPECT_EQ(r.out.lines.back(), "S,3,3,0,-6.25,-1.50,-104.0,0");
}

TEST(Burst, GoRunsUntilStopWithoutSummary) {
  Rig r;
  Sim s(*r.p);
  s.cmd("GO");
  EXPECT_EQ(r.out.lines.front(), "#OK go");
  s.run(2000);
  s.cmd("STOP");
  s.run(6000);
  EXPECT_EQ(s.tx_starts.size(), 2u);  // inicios en 0 y 1500
  EXPECT_EQ(r.out.count("S,"), 0u);
  EXPECT_EQ(r.out.count("P,"), 2u);
  s.cmd("PING");  // quedó libre
  EXPECT_EQ(s.tx_starts.size(), 3u);
}

TEST(Burst, StopWhileWaitingBetweenPings) {
  Rig r;
  Sim s(*r.p);
  s.cmd("PING 5");
  s.run(1450);  // ping 1 terminó (1384), el siguiente espera hasta 1500
  s.cmd("STOP");
  EXPECT_EQ(r.out.lines.back(), "S,1,1,0,-6.25,-1.50,-104.0,0");
  s.run(5000);
  EXPECT_EQ(s.tx_starts.size(), 1u);
}

TEST(Burst, RangeChecks) {
  Rig r;
  EXPECT_EQ(r.p->onCommand("PING 51", 0).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR range ping 1-50");
  EXPECT_EQ(r.p->onCommand("PING 0", 0).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR range ping 1-50");
  r.p->onCommand("SET rate=200", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR range rate>=1000");
  EXPECT_EQ(r.p->config().rate_ms, 1500u);
}

TEST(Burst, PingWhileBusyIsRejected) {
  Rig r;
  r.p->onCommand("PING", 0);
  r.p->onCommand("PING", 1);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
}

TEST(Burst, DutyLimiterDelaysGoInsteadOfDropping) {
  Rig r;
  Sim s(*r.p);
  s.cmd("GO");
  s.run(59999);
  // 10 % de 60 s = 6000 ms; con ToA 177 ms caben 33 TX
  EXPECT_LE(s.tx_starts.size(), 33u);
  EXPECT_GE(s.tx_starts.size(), 30u);
  EXPECT_GE(r.out.count("#WARN duty_wait"), 1u);
  EXPECT_LE(r.out.count("P,"), s.tx_starts.size());      // a lo sumo uno en curso
  EXPECT_GE(r.out.count("P,") + 1, s.tx_starts.size());
}

TEST(Burst, DifferentHashChangeAppliesToNextPing) {
  Rig r;
  Sim s(*r.p);
  s.cmd("PING 2");
  s.run(100);
  r.p->onCommand("SET hash=B2", s.t);  // con el ping 1 en vuelo: se aplica al siguiente
  s.run(3000);
  EXPECT_EQ(r.out.count("P,"), 2u);
  EXPECT_EQ(s.req[11], 0xB2);
}
