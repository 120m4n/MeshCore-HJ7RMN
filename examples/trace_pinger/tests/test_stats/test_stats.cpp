#include <gtest/gtest.h>

#include "tracecore/Stats.h"

using namespace tracecore;

TEST(Stats, OddEvenSingleEmpty) {
  int32_t out = 0;
  const int32_t odd[] = {-300, -625, -100};
  ASSERT_TRUE(median(odd, 3, out));
  EXPECT_EQ(out, -300);
  const int32_t even[] = {-625, -450};  // promedio -537.5 -> -538 (lejos de cero)
  ASSERT_TRUE(median(even, 2, out));
  EXPECT_EQ(out, -538);
  const int32_t one[] = {42};
  ASSERT_TRUE(median(one, 1, out));
  EXPECT_EQ(out, 42);
  EXPECT_FALSE(median(one, 0, out));
}

TEST(Stats, RejectsMoreThanBurstMax) {
  int32_t v[51] = {0}, out;
  EXPECT_FALSE(median(v, 51, out));
}

#include "../support/test_main.h"
