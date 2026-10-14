// US1-AS4: las tramas inválidas del contrato las rechaza parseEcho; el parser upstream
// coincide cuando la trama es estructuralmente inválida (path_len con hash de 4 bytes).
#include <Packet.h>
#include <gtest/gtest.h>

#include <vector>

#include "tracecore/TraceFrame.h"

using mesh::Packet;

static const uint8_t kGood[13] = {0x26, 0x01, 0xE7, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x00, 0xA1};

static std::vector<uint8_t> good() { return std::vector<uint8_t>(kGood, kGood + 13); }
static bool echoOk(const std::vector<uint8_t>& f) { return tracecore::parseEcho(f.data(), f.size(), 0xA1).has_value(); }
static bool upstreamOk(const std::vector<uint8_t>& f) { Packet p; return p.readFrom(f.data(), (uint8_t)f.size()); }

TEST(Rejections, ControlFrameIsAcceptedByBoth) {
  EXPECT_TRUE(echoOk(good()));
  EXPECT_TRUE(upstreamOk(good()));
}

TEST(Rejections, WrongHeader) {
  auto f = good(); f[0] = 0x25;  // TRACE pero FLOOD
  EXPECT_FALSE(echoOk(f));
  EXPECT_TRUE(upstreamOk(f));  // estructuralmente válida: la rechaza solo nuestro filtro
  f[0] = 0x24;                 // tipo ADVERT-2.. otro tipo
  EXPECT_FALSE(echoOk(f));
}

TEST(Rejections, WrongPathLen) {
  for (uint8_t pl : {0x00, 0x02, 0x03}) {
    auto f = good(); f[1] = pl;
    EXPECT_FALSE(echoOk(f));
  }
}

TEST(Rejections, WrongLength) {
  auto shorter = good(); shorter.pop_back();
  auto longer = good(); longer.push_back(0x00);
  EXPECT_FALSE(echoOk(shorter));
  EXPECT_FALSE(echoOk(longer));
}

TEST(Rejections, WrongFlagsOrHash) {
  auto f = good(); f[11] = 0x01;
  EXPECT_FALSE(echoOk(f));
  EXPECT_TRUE(upstreamOk(f));
  auto g = good(); g[12] = 0xA2;
  EXPECT_FALSE(echoOk(g));
  EXPECT_TRUE(upstreamOk(g));
}

TEST(Rejections, PathLenWithHashSize4IsRejectedByBoth) {
  auto f = good(); f[1] = 0xC1;  // bits 6-7 = 3 -> hash de 4 bytes (isValidPathLen = false)
  EXPECT_FALSE(Packet::isValidPathLen(0xC1));
  EXPECT_FALSE(echoOk(f));
  EXPECT_FALSE(upstreamOk(f));
}
