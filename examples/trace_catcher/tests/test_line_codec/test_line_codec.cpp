#include <gtest/gtest.h>

#include <string>

#include "tracecore/LineCodec.h"

using namespace tracecore;
using K = Command::Kind;

TEST(LineCodec, FormatMeas) {
  RssiStats r;
  r.n = 64; r.kept = 61; r.mean = -65.025f; r.sd = 0.41f; r.median = -65.0f; r.min = -65.5f; r.max = -64.5f;
  char b[kMaxLine];
  formatMeas(b, sizeof b, r);
  EXPECT_STREQ(b, "M,64,61,-65.03,0.41,-65.0,-65.5,-64.5");
}

TEST(LineCodec, ParseCommands) {
  EXPECT_TRUE(parseCommand("hello\r").kind == K::Hello);
  EXPECT_TRUE(parseCommand("CFG?").kind == K::CfgQuery);
  EXPECT_TRUE(parseCommand("").kind == K::Empty);
  for (const char* gone : {"FOO", "PING", "GO", "STOP", "MON on", "RF on", "PONG GET", "SAVE", "LAST?"})
    EXPECT_TRUE(parseCommand(gone).kind == K::Unknown);

  Command m = parseCommand("meas");
  EXPECT_TRUE(m.kind == K::Meas);
  EXPECT_EQ(m.n, kMeasDefault);
  m = parseCommand("MEAS 32");
  EXPECT_TRUE(m.kind == K::Meas);
  EXPECT_EQ(m.n, 32u);
  EXPECT_TRUE(parseCommand("MEAS x").kind == K::Syntax);
  EXPECT_TRUE(parseCommand("MEAS 32 wait=5").kind == K::Syntax);
  EXPECT_TRUE(parseCommand("MEAS 99999999999").kind == K::Syntax);

  Command s = parseCommand("SET Freq=914.000");
  EXPECT_TRUE(s.kind == K::Set);
  EXPECT_STREQ(s.key, "Freq");
  EXPECT_STREQ(s.val, "914.000");
  EXPECT_TRUE(parseCommand("SET freq").kind == K::Syntax);
  EXPECT_TRUE(parseCommand("SET =1").kind == K::Syntax);
}

#include "../support/test_main.h"
