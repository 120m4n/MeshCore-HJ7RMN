// US1-AS2: lo que serializa el core upstream para un eco lo entiende parseEcho, para todo SNR.
#include <Packet.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <random>

#include "tracecore/TraceFrame.h"

using mesh::Packet;

constexpr uint32_t kSeed = 0x59414749;

TEST(WriteToVsParseEcho, AllSnrValuesTimes16Hashes) {
  std::mt19937 rng(kSeed);
  for (int q = -128; q <= 127; q++) {
    for (int k = 0; k < 16; k++) {
      const uint8_t hash = (uint8_t)(k * 17);
      const uint32_t tag = rng();

      Packet p;
      p.header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
      p.setPathHashSizeAndCount(1, 1);
      p.path[0] = (uint8_t)(int8_t)q;
      p.payload_len = 10;
      for (int i = 0; i < 4; i++) p.payload[i] = (tag >> (8 * i)) & 0xFF;
      for (int i = 4; i < 9; i++) p.payload[i] = 0;  // auth_code (4) + flags (1)
      p.payload[9] = hash;

      uint8_t wire[MAX_TRANS_UNIT];
      const uint8_t n = p.writeTo(wire);
      const auto e = tracecore::parseEcho(wire, n, hash);
      const bool good = n == tracecore::kEchoLen && e.has_value() && e->snr_up_q == q && e->tag == tag;
      EXPECT_TRUE(good);
      if (!good) {
        std::printf("  seed=0x%08X q=%d hash=%02X len=%d\n", kSeed, q, hash, n);
        return;
      }
    }
  }
}
