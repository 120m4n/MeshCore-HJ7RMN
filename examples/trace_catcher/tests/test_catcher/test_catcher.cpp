#include <gtest/gtest.h>

#include "Fakes.h"

using namespace tracecore;
using fakes::Rig;

TEST(Catcher, BootSaysHelloAndCfg) {
  Rig r;
  r.p->emitBoot();
  ASSERT_EQ(r.out.lines.size(), 2u);
  EXPECT_EQ(r.out.lines[0], "#HELLO fw=0.6.0 proto=2 role=catcher radio=SX1262 id=C3F01A");
  EXPECT_EQ(r.out.lines[1], "#CFG freq=915.800 bw=250 sf=10 cr=5");
}

TEST(Catcher, SetFreqReconfiguresAndRejectsTheRest) {
  Rig r;
  Action a = r.p->onCommand("SET freq=914.000");
  EXPECT_TRUE(a.kind == Action::Reconfigure);
  EXPECT_EQ(r.p->config().freq_khz, 914000u);
  EXPECT_EQ(r.out.lines.back(), "#OK freq=914.000");

  a = r.p->onCommand("SET freq=900");
  EXPECT_TRUE(a.kind == Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR range freq 902.000-928.000");
  EXPECT_EQ(r.p->config().freq_khz, 914000u);   // un SET inválido no cambia nada

  r.p->onCommand("SET hash=A1");
  EXPECT_EQ(r.out.lines.back(), "!ERR unknown hash");
  r.p->onCommand("SET freq=x");
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
}

TEST(Catcher, MeasReturnsAMeasureActionWithinRange) {
  Rig r;
  Action a = r.p->onCommand("MEAS 32");
  EXPECT_TRUE(a.kind == Action::Measure);
  EXPECT_EQ(a.n, 32);
  a = r.p->onCommand("MEAS");
  EXPECT_EQ(a.n, kMeasDefault);
  a = r.p->onCommand("MEAS 15");
  EXPECT_TRUE(a.kind == Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR range meas 16-256");
  r.p->onCommand("MEAS 257");
  EXPECT_EQ(r.out.lines.back(), "!ERR range meas 16-256");
  r.p->onCommand("MEAS x");
  EXPECT_EQ(r.out.lines.back(), "!ERR syntax");
}

TEST(Catcher, RemovedCommandsAreUnknown) {
  Rig r;
  for (const char* c : {"PING", "GO", "STOP", "MON on", "RF off", "PONG GET", "SAVE", "LAST?"}) {
    EXPECT_TRUE(r.p->onCommand(c).kind == Action::None);
    EXPECT_EQ(r.out.lines.back(), "!ERR unknown");
  }
}

#include "../support/test_main.h"
