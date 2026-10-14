#include <gtest/gtest.h>

#include "tracecore/DutyLimiter.h"

using namespace tracecore;

TEST(DutyLimiter, Allows45TxPerMinuteAt132ms) {
  DutyLimiter d;  // 60 s, 6 s de TX
  for (uint32_t i = 0; i < 45; i++) {
    ASSERT_TRUE(d.canTransmit(i * 1000, 132));
    d.record(i * 1000, 132);
  }
  EXPECT_FALSE(d.canTransmit(45000, 132));       // la 46.ª espera
  EXPECT_EQ(d.nextAllowed(45000, 132), 60000u);  // cuando caduca la primera
  EXPECT_TRUE(d.canTransmit(60000, 132));
}

TEST(DutyLimiter, ClockWrap) {
  DutyLimiter d;
  const uint32_t t0 = 0xFFFFFF00u;
  d.record(t0, 5000);
  EXPECT_FALSE(d.canTransmit(t0 + 100, 2000));
  EXPECT_TRUE(d.canTransmit(t0 + 60000, 2000));
}

#include "../support/test_main.h"
