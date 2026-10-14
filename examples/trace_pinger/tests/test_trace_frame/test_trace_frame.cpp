#include <gtest/gtest.h>

#include <vector>

#include "tracecore/TraceFrame.h"

using namespace tracecore;

TEST(TraceFrame, BuildMatchesContract) {
  uint8_t b[kRequestLen];
  buildRequest(0x78563412, 0xA1, b);
  const uint8_t want[] = {0x26, 0x00, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x00, 0xA1};
  for (int i = 0; i < 12; i++) EXPECT_EQ(b[i], want[i]);
}

static const uint8_t kEcho[13] = {0x26, 0x01, 0xE7, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x00, 0xA1};

TEST(TraceFrame, ParseValidEcho) {
  auto e = parseEcho(kEcho, 13, 0xA1);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->tag, 0x78563412u);
  EXPECT_EQ(e->snr_up_q, -25);
}

TEST(TraceFrame, RejectsMalformed) {
  std::vector<uint8_t> v(kEcho, kEcho + 13);
  EXPECT_FALSE(parseEcho(v.data(), 12, 0xA1).has_value());   // longitud
  EXPECT_FALSE(parseEcho(v.data(), 13, 0xA2).has_value());   // hash distinto
  auto bad = v; bad[0] = 0x25;  EXPECT_FALSE(parseEcho(bad.data(), 13, 0xA1).has_value());
  bad = v;      bad[1] = 0x00;  EXPECT_FALSE(parseEcho(bad.data(), 13, 0xA1).has_value());
  bad = v;      bad[1] = 0x02;  EXPECT_FALSE(parseEcho(bad.data(), 13, 0xA1).has_value());
  bad = v;      bad[11] = 0x01; EXPECT_FALSE(parseEcho(bad.data(), 13, 0xA1).has_value());
}

TEST(TraceFrame, SnrExtremes) {
  std::vector<uint8_t> v(kEcho, kEcho + 13);
  v[2] = 0x80;
  EXPECT_EQ(parseEcho(v.data(), 13, 0xA1)->snr_up_q, -128);  // -32.00 dB
  v[2] = 0x7F;
  EXPECT_EQ(parseEcho(v.data(), 13, 0xA1)->snr_up_q, 127);   // +31.75 dB
}

#include "../support/test_main.h"
