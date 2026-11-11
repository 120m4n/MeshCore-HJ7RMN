#include <gtest/gtest.h>

#include "tracecore/Stats.h"

using namespace tracecore;

TEST(Stats, RssiDropsLoRaBurstsAndKeepsHalfDb) {
  int16_t v[64];
  for (int i = 0; i < 64; i++) v[i] = (int16_t)(-130 + (i % 3) - 1);   // −65,5 / −65,0 / −64,5 dBm
  v[5] = v[20] = v[40] = -100;                                         // ráfagas ajenas a −50 dBm
  RssiStats r;
  ASSERT_TRUE(rssiStats(v, 64, r));
  EXPECT_EQ(r.n, 64);
  EXPECT_EQ(r.kept, 61);
  EXPECT_NEAR(r.mean, -65.025f, 0.005f);   // (22·−65,5 + 20·−65 + 19·−64,5)/61
  EXPECT_NEAR(r.sd, 0.41f, 0.02f);
  EXPECT_NEAR(r.median, -65.0f, 1e-4f);
  EXPECT_NEAR(r.min, -65.5f, 1e-4f);
  EXPECT_NEAR(r.max, -64.5f, 1e-4f);
}

TEST(Stats, RssiConstantAndLimits) {
  int16_t v[kMeasMax + 1];
  for (auto& x : v) x = -181;   // −90,5 dBm
  RssiStats r;
  ASSERT_TRUE(rssiStats(v, 16, r));
  EXPECT_EQ(r.kept, 16);
  EXPECT_NEAR(r.mean, -90.5f, 1e-4f);
  EXPECT_NEAR(r.sd, 0.0f, 1e-4f);
  EXPECT_FALSE(rssiStats(v, 1, r));
  EXPECT_FALSE(rssiStats(v, kMeasMax + 1, r));
}

#include "../support/test_main.h"
