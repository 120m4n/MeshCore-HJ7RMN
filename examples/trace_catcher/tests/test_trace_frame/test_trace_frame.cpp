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

TEST(TraceFrame, FourByteHashMatchesContract) {  // CLAUDE.md §14
  const TraceHash h(0xA1B2C3D4, 4);
  uint8_t b[kRequestMax];
  ASSERT_EQ(buildRequest(0x78563412, h, b), 15u);
  const uint8_t want[] = {0x26, 0x00, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x02, 0xA1, 0xB2, 0xC3, 0xD4};
  for (int i = 0; i < 15; i++) EXPECT_EQ(b[i], want[i]);

  const uint8_t echo[16] = {0x26, 0x01, 0xE7, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x02, 0xA1, 0xB2, 0xC3, 0xD4};
  auto e = parseEcho(echo, 16, h);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->tag, 0x78563412u);
  EXPECT_EQ(e->snr_up_q, -25);
  EXPECT_FALSE(parseEcho(echo, 16, TraceHash(0xA1B2C3D5, 4)).has_value());  // otro ponger, mismo 1er byte
  EXPECT_FALSE(parseEcho(echo, 16, TraceHash(0xA1)).has_value());           // tamaño distinto
  EXPECT_FALSE(parseEcho(kEcho, 13, h).has_value());                         // eco de 1 byte
}

#include "../support/test_main.h"
