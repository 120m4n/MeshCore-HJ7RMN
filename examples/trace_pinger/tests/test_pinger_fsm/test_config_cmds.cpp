#include <gtest/gtest.h>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

TEST(ConfigCmds, HelloAndCfg) {
  Rig r;
  r.p->onCommand("HELLO", 0);
  EXPECT_EQ(r.out.lines.back(), "#HELLO fw=0.1.0 proto=1 radio=SX1262 id=C3F01A");
  r.p->onCommand("CFG?", 0);
  EXPECT_EQ(r.out.lines.back(), "#CFG freq=915.800 bw=250 sf=10 cr=5 txp=22 hash=A1 rate=1500 timeout=auto raw=0");
}

TEST(ConfigCmds, EmitBootAnnouncesHelloAndCfg) {
  Rig r(false);
  r.p->emitBoot(true);
  ASSERT_EQ(r.out.lines.size(), 3u);
  EXPECT_EQ(r.out.lines[0].substr(0, 6), "#HELLO");
  EXPECT_EQ(r.out.lines[1], "#WARN cfg_reset");
  EXPECT_EQ(r.out.lines[2], "#CFG freq=915.800 bw=250 sf=10 cr=5 txp=22 hash= rate=1500 timeout=auto raw=0");
}

TEST(ConfigCmds, ValidSetEchoesAndRadioKeysReconfigure) {
  Rig r;
  EXPECT_EQ(r.p->onCommand("SET freq=910.525", 0).kind, Action::Reconfigure);
  EXPECT_EQ(r.out.lines.back(), "#OK freq=910.525");
  EXPECT_EQ(r.p->config().radio.freq_khz, 910525u);
  EXPECT_EQ(r.p->onCommand("SET rate=2000", 0).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "#OK rate=2000");
  EXPECT_EQ(r.p->onCommand("SET txp=22", 0).kind, Action::Reconfigure);  // sin tope por firmware
  r.p->onCommand("CFG?", 0);
  EXPECT_EQ(r.out.lines.back(), "#CFG freq=910.525 bw=250 sf=10 cr=5 txp=22 hash=A1 rate=2000 timeout=auto raw=0");
}

TEST(ConfigCmds, InvalidSetKeepsState) {
  Rig r;
  EXPECT_EQ(r.p->onCommand("SET sf=13", 0).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR range sf 5-12");
  r.p->onCommand("SET nope=1", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR unknown nope");
  r.p->onCommand("SET sf", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
  r.p->onCommand("BOGUS", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR unknown");
  EXPECT_EQ(r.p->config().radio.sf, 10);
}

TEST(ConfigCmds, RadioKeyRejectedWhilePingPending) {
  Rig r;
  r.p->onCommand("PING", 0);
  EXPECT_EQ(r.p->onCommand("SET freq=915.000", 1).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
  EXPECT_EQ(r.p->config().radio.freq_khz, 915800u);
}

TEST(ConfigCmds, HashCanBeSetAtAnyTimeAndWarnsWhenSuspicious) {
  Rig r(false);
  r.p->onCommand("SET hash=00", 0);
  EXPECT_EQ(r.out.lines[r.out.lines.size() - 2], "#OK hash=00");
  EXPECT_EQ(r.out.lines.back(), "#WARN hash_suspicious");
  r.p->onCommand("SET hash=ff", 0);
  EXPECT_EQ(r.out.lines.back(), "#WARN hash_suspicious");
  r.p->onCommand("SET hash=A1", 0);
  EXPECT_EQ(r.out.lines.back(), "#OK hash=A1");
  EXPECT_EQ(*r.p->config().hash, 0xA1);
}

TEST(ConfigCmds, SaveAsksShellToPersist) {
  Rig r;
  EXPECT_EQ(r.p->onCommand("SAVE", 0).kind, Action::Save);
  r.p->onSaveDone(true);
  EXPECT_EQ(r.out.lines.back(), "#OK saved");
  r.p->onSaveDone(false);
  EXPECT_EQ(r.out.lines.back(), "!ERR state flash");
}
