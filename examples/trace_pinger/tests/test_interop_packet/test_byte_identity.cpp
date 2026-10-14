// US1-AS3: buildRequest() produce exactamente los bytes que escribe el core upstream (writeTo).
#include <Packet.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <random>

#include "tracecore/TraceFrame.h"

using mesh::Packet;

constexpr uint32_t kSeed = 0x59414749;

TEST(ByteIdentity, BuildRequestEqualsUpstreamWriteTo) {
  std::mt19937 rng(kSeed);
  for (int i = 0; i < 1000; i++) {
    const uint32_t tag = rng();
    const uint8_t hash = (uint8_t)rng();

    Packet p;  // armado a mano con la API upstream
    p.header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
    p.setPathHashSizeAndCount(1, 0);
    p.payload_len = 10;
    for (int k = 0; k < 4; k++) p.payload[k] = (tag >> (8 * k)) & 0xFF;
    for (int k = 4; k < 9; k++) p.payload[k] = 0;
    p.payload[9] = hash;
    uint8_t upstream[MAX_TRANS_UNIT];
    const uint8_t n = p.writeTo(upstream);

    uint8_t ours[tracecore::kRequestLen];
    tracecore::buildRequest(tag, hash, ours);

    const bool same = n == sizeof ours && std::memcmp(ours, upstream, n) == 0;
    EXPECT_TRUE(same);
    if (!same) {
      std::printf("  seed=0x%08X iter=%d tag=%08X hash=%02X\n", kSeed, i, (unsigned)tag, hash);
      return;
    }
  }
}
