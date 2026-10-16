#include <gtest/gtest.h>
#include "../../src/helpers/tv_batt.h"

TEST(TvBatt, LinearRange) {
  EXPECT_EQ(0, tv::batt_pct(3300));
  EXPECT_EQ(50, tv::batt_pct(3750));
  EXPECT_EQ(100, tv::batt_pct(4200));
}

TEST(TvBatt, ClampsInsideValidWindow) {
  EXPECT_EQ(0, tv::batt_pct(3100));
  EXPECT_EQ(100, tv::batt_pct(4250));
}

TEST(TvBatt, OutOfWindowMeansExternalPower) {
  EXPECT_EQ(100, tv::batt_pct(0));
  EXPECT_EQ(100, tv::batt_pct(2500));
  EXPECT_EQ(100, tv::batt_pct(5000));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
