// T004: utilidades de hex que usan los fixtures.
#include <gtest/gtest.h>

#include "Hex.h"

using namespace tracecore_test;

TEST(Hex, ToHexIsUppercaseWithoutSeparators) {
  const uint8_t b[] = {0x26, 0x01, 0xE7, 0x00, 0xA1};
  EXPECT_EQ(toHex(b, sizeof b), "2601E700A1");
  EXPECT_EQ(toHex(std::vector<uint8_t>{}), "");
}

TEST(Hex, FromHexAcceptsBothCases) {
  std::vector<uint8_t> v;
  ASSERT_TRUE(fromHex("2601e7A1", v));
  EXPECT_EQ(toHex(v), "2601E7A1");
}

TEST(Hex, FromHexRejectsOddLengthAndNonHex) {
  std::vector<uint8_t> v = {1, 2, 3};
  EXPECT_FALSE(fromHex("260", v));
  EXPECT_FALSE(fromHex("26ZZ", v));
  EXPECT_TRUE(v.empty());
  EXPECT_TRUE(fromHex("", v));
}

TEST(Hex, RoundtripAllBytes) {
  std::vector<uint8_t> all;
  for (int i = 0; i < 256; i++) all.push_back((uint8_t)i);
  std::vector<uint8_t> back;
  ASSERT_TRUE(fromHex(toHex(all), back));
  EXPECT_TRUE(all == back);
}
