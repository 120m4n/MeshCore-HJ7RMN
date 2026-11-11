#include <gtest/gtest.h>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

// PING n wait=s: el usuario se aleja (y pierde el BLE); la ráfaga arranca sola y se recoge con LAST?.
TEST(Wait, StartsAfterWaitSurvivesBleLossAndReplaysWithLast) {
  Rig r;
  Sim s(*r.p);
  s.handle(r.p->onCommand("PING 3 wait=20", 0, Origin::Ble));
  EXPECT_EQ(r.out.lines.back().rfind("#OK ping n=3 wait=20 eta=", 0), 0u);
  EXPECT_TRUE(s.tx_starts.empty());
  s.run(19999);
  EXPECT_TRUE(s.tx_starts.empty());          // nada sale durante la espera
  r.p->onCommand("LAST?", s.t);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
  s.run(20500);
  ASSERT_EQ(s.tx_starts.size(), 1u);
  EXPECT_EQ(s.tx_starts[0], 20000u);
  EXPECT_EQ(r.p->onTransportLost(Origin::Ble).kind, Action::None);   // se fue el BLE: sigue
  s.run(30000);
  EXPECT_EQ(s.tx_starts.size(), 3u);
  const std::string sum = r.out.lines.back();
  EXPECT_EQ(sum.rfind("S,3,3,0,", 0), 0u);

  const size_t before = r.out.lines.size();
  r.p->onCommand("LAST?", s.t);
  ASSERT_EQ(r.out.lines.size(), before + 5);   // #OK last + 3 P + S
  EXPECT_EQ(r.out.lines[before], "#OK last n=3");
  EXPECT_EQ(r.out.lines[before + 1].rfind("P,1,", 0), 0u);
  EXPECT_EQ(r.out.lines.back(), sum);
}

TEST(Wait, PlainBurstStillStopsOnBleLossAndStopEndsWait) {
  Rig r;
  Sim s(*r.p);
  r.p->onCommand("LAST?", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR state nolast");
  s.handle(r.p->onCommand("PING 5", 0, Origin::Ble));
  s.run(500);
  r.p->onTransportLost(Origin::Ble);
  EXPECT_EQ(r.out.lines.back(), "#WARN ble_lost_stop");

  Rig r2;
  Sim s2(*r2.p);
  s2.cmd("PING 5 wait=10");
  s2.cmd("STOP");
  s2.run(20000);
  EXPECT_TRUE(s2.tx_starts.empty());
  r2.p->onCommand("LAST?", s2.t);
  EXPECT_EQ(r2.out.lines.back(), "S,0,0,,,,,0");
}

TEST(Wait, Syntax) {
  Rig r;
  for (const char* bad : {"PING 3 wait=", "PING 3 espera=2", "PING 3 wait=x"}) {
    r.p->onCommand(bad, 0);
    EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
  }
  r.p->onCommand("PING 3 wait=121", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR range wait 0-120");
}

// La ETA incluye las esperas del ciclo de TX: con el DutyLimiter lleno, la S llega antes de la ETA, no después.
TEST(Wait, EtaCoversDutyWait) {
  Rig r;
  Sim s(*r.p);
  s.cmd("PING 50 wait=1");
  const std::string ok = r.out.lines.back();
  const uint32_t eta = (uint32_t)std::stoul(ok.substr(ok.find("eta=") + 4));
  s.run(eta * 1000);
  EXPECT_EQ(r.out.lines.back().rfind("S,50,", 0), 0u);
  EXPECT_GE(r.out.count("#WARN duty_wait"), 1u);   // de verdad hubo espera del ciclo de TX
}
