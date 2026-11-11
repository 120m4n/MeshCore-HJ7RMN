#include <gtest/gtest.h>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

TEST(Mode, StartsAsCatcherAndNeverTransmits) {
  Rig r(true, false);
  EXPECT_EQ(r.p->mode(), Mode::Catcher);
  for (const char* c : {"PING", "PING 5", "GO"}) {
    EXPECT_EQ(r.p->onCommand(c, 0).kind, Action::None);
    EXPECT_EQ(r.out.lines.back(), "!ERR mode");
  }
}

TEST(Mode, CatcherKeepsHelloCfgSetStop) {
  Rig r(true, false);
  r.p->onCommand("HELLO", 0);
  EXPECT_NE(r.out.lines.back().find("proto=3 role=catcher"), std::string::npos);
  EXPECT_EQ(r.p->onCommand("SET freq=915.0", 0).kind, Action::Reconfigure);
  r.p->onCommand("STOP", 0);
  EXPECT_EQ(r.out.lines.back(), "#OK stop");
}

TEST(Mode, SwitchToPingerAllowsPingAndBack) {
  Rig r(true, false);
  r.p->onCommand("MODE pinger", 0);
  EXPECT_EQ(r.out.lines.back(), "#OK mode=pinger");
  EXPECT_EQ(r.p->onCommand("PING", 1000).kind, Action::Transmit);
  r.p->onCommand("MODE catcher", 1001);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");   // con un ping en vuelo no se cambia
  r.p->onTxDone(1177, true);
  r.p->tick(3000);
  r.p->onCommand("MODE catcher", 3001);
  EXPECT_EQ(r.out.lines.back(), "#OK mode=catcher");
  EXPECT_EQ(r.p->onCommand("PING", 3002).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR mode");
}

TEST(Mode, BadArgIsSyntaxError) {
  Rig r(true, false);
  r.p->onCommand("MODE", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
  r.p->onCommand("MODE lab", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
  EXPECT_EQ(r.p->mode(), Mode::Catcher);
}

TEST(Mode, SwitchingResetsTheRadioPreset) {
  Rig r(true, false);
  EXPECT_EQ(r.p->onCommand("SET freq=910.000", 0).kind, Action::Reconfigure);
  EXPECT_EQ(r.p->onCommand("MODE pinger", 0).kind, Action::Reconfigure);
  EXPECT_EQ(r.p->config().radio.freq_khz, RadioConfig().freq_khz);
}
