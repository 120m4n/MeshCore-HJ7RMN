#include <gtest/gtest.h>

#include "tracecore/Config.h"

using namespace tracecore;

TEST(Config, SetFreqValidAndInvalid) {
  RadioConfig c;
  EXPECT_TRUE(applySet(c, "freq", "910.525").result == SetResult::Ok);
  EXPECT_EQ(c.freq_khz, 910525u);
  EXPECT_TRUE(applySet(c, "FREQ", "914").result == SetResult::Ok);   // clave sin distinguir mayúsculas, sin decimales
  EXPECT_EQ(c.freq_khz, 914000u);

  auto o = applySet(c, "freq", "900");
  EXPECT_TRUE(o.result == SetResult::Range);
  EXPECT_STREQ(o.detail, "freq 902.000-928.000");
  EXPECT_TRUE(applySet(c, "freq", "929").result == SetResult::Range);
  EXPECT_TRUE(applySet(c, "freq", "914.0001").result == SetResult::Syntax);
  EXPECT_TRUE(applySet(c, "freq", "x").result == SetResult::Syntax);
  EXPECT_TRUE(applySet(c, "freq", "-910").result == SetResult::Syntax);
  EXPECT_TRUE(applySet(c, "sf", "7").result == SetResult::Unknown);
  EXPECT_EQ(c.freq_khz, 914000u);   // un SET inválido no cambia nada
}

TEST(Config, FormatCfg) {
  RadioConfig c;
  char b[160];
  formatCfg(b, sizeof b, c);
  EXPECT_STREQ(b, "#CFG freq=915.800 bw=250 sf=10 cr=5");
  c.freq_khz = 914000;
  c.bw_x100 = 6250;
  formatCfg(b, sizeof b, c);
  EXPECT_STREQ(b, "#CFG freq=914.000 bw=62.5 sf=10 cr=5");
}

#include "../support/test_main.h"
