// US1-AS1: toda petición que arma tracecore la acepta el parser upstream con los campos esperados.
#include <Packet.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <random>

#include "tracecore/TraceFrame.h"

using mesh::Packet;

constexpr uint32_t kSeed = 0x59414749;  // "YAGI" (spec 002 FR-006)

TEST(BuildVsReadFrom, TenThousandRandomPairs) {
  std::mt19937 rng(kSeed);
  for (int i = 0; i < 10000; i++) {
    const uint32_t tag = rng();
    const uint8_t hash = (uint8_t)rng();

    uint8_t req[tracecore::kRequestLen];
    tracecore::buildRequest(tag, hash, req);

    Packet p;
    const bool ok = p.readFrom(req, sizeof req);
    const bool fields = ok && p.getRouteType() == ROUTE_TYPE_DIRECT &&
                        p.getPayloadType() == PAYLOAD_TYPE_TRACE && p.getPayloadVer() == 0 &&
                        p.getPathHashCount() == 0 && p.payload_len == 10 &&
                        std::memcmp(p.payload, req + 2, 10) == 0;  // tag ‖ auth ‖ flags ‖ H
    EXPECT_TRUE(fields);
    if (!fields) {
      std::printf("  seed=0x%08X iter=%d tag=%08X hash=%02X\n", kSeed, i, (unsigned)tag, hash);
      return;
    }
  }
}

TEST(BuildVsReadFrom, PayloadCarriesTagAuthFlagsHash) {
  uint8_t req[tracecore::kRequestLen];
  tracecore::buildRequest(0x78563412, 0xA1, req);
  Packet p;
  ASSERT_TRUE(p.readFrom(req, sizeof req));
  const uint8_t want[10] = {0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0x00, 0xA1};
  EXPECT_EQ(std::memcmp(p.payload, want, 10), 0);
}
