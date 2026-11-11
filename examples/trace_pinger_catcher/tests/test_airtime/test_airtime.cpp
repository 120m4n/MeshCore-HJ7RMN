#include <gtest/gtest.h>

#include "tracecore/Airtime.h"

using namespace tracecore;

static RadioConfig preset(uint8_t sf, uint16_t bw_x100) {
  RadioConfig r;
  r.sf = sf;
  r.bw_x100 = bw_x100;
  return r;
}

TEST(Airtime, ToaMatchesResearchTable) {
  // research R-3: ToA petición 12 B y eco 13 B, ±1 ms
  EXPECT_NEAR(toaMs(preset(7, 6250), 12), 132, 1);
  EXPECT_NEAR(toaMs(preset(7, 6250), 13), 142, 1);
  EXPECT_NEAR(toaMs(preset(8, 6250), 12), 263, 1);
  EXPECT_NEAR(toaMs(preset(8, 6250), 13), 263, 1);
  EXPECT_NEAR(toaMs(preset(10, 25000), 12), 177, 1);
  EXPECT_NEAR(toaMs(preset(10, 25000), 13), 177, 1);
  EXPECT_NEAR(toaMs(preset(11, 25000), 12), 354, 1);
}

TEST(Airtime, DefaultPresetIsAustralia) {
  RadioConfig r;
  EXPECT_EQ(r.freq_khz, 915800u);
  EXPECT_EQ(r.bw_x100, 25000);
  EXPECT_EQ(r.sf, 10);
  EXPECT_EQ(r.cr, 5);
  EXPECT_EQ(r.txp_dbm, 22);
  EXPECT_EQ(r.preamble(), 16);
}

TEST(Airtime, TimeoutAutoMatchesResearchTable) {
  EXPECT_NEAR(timeoutAutoMs(preset(7, 6250)), 960, 1);
  EXPECT_NEAR(timeoutAutoMs(preset(8, 6250)), 1658, 1);
  EXPECT_NEAR(timeoutAutoMs(preset(10, 25000)), 1207, 1);
  EXPECT_NEAR(timeoutAutoMs(preset(11, 25000)), 2090, 1);
}

#include "../support/test_main.h"
