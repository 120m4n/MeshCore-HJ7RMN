#include <gtest/gtest.h>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

// RF off: cambio de antena seguro. La radio duerme y nada puede transmitir hasta RF on.
TEST(Rf, OffBlocksEveryTransmitUntilOn) {
  Rig r;
  r.p->onCommand("MON on", 0);
  EXPECT_EQ(r.p->onCommand("RF off", 1).kind, Action::RadioOff);
  EXPECT_EQ(r.out.lines.back(), "#OK rf=off");
  EXPECT_TRUE(r.p->rfOff());
  for (const char* cmd : {"PING", "PING 5", "GO", "PONG GET", "MON on"}) {
    EXPECT_EQ(r.p->onCommand(cmd, 2).kind, Action::None);
    EXPECT_EQ(r.out.lines.back(), "!ERR rfoff");
  }
  EXPECT_EQ(r.p->tick(100000).kind, Action::None);
  EXPECT_EQ(r.p->onCommand("RF on", 3).kind, Action::StartRx);
  EXPECT_EQ(r.out.lines.back(), "#OK rf=on");
  EXPECT_FALSE(r.p->rfOff());
  EXPECT_EQ(r.p->onCommand("PING", 4).kind, Action::Transmit);
}

TEST(Rf, OffWhilePingInFlightIsBusy) {
  Rig r;
  r.p->onCommand("PING", 0);
  EXPECT_EQ(r.p->onCommand("RF off", 1).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
  EXPECT_FALSE(r.p->rfOff());
}

TEST(Rf, OnWhenAlreadyOnIsNoop) {
  Rig r;
  EXPECT_EQ(r.p->onCommand("RF on", 0).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "#OK rf=on");
  r.p->onCommand("RF bogus", 1);
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
}
