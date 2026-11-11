// Regresión del riesgo de research R-2: el Packet.cpp REAL del core compila y enlaza en host
// con los mocks de test/mocks, y readFrom/writeTo funcionan.
#include <Packet.h>
#include <gtest/gtest.h>

#include <cstring>

using mesh::Packet;

TEST(InteropSmoke, UpstreamPacketRoundtrip) {
  Packet p;
  p.header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
  p.setPathHashSizeAndCount(1, 0);
  p.payload_len = 3;
  p.payload[0] = 1; p.payload[1] = 2; p.payload[2] = 3;

  uint8_t wire[MAX_TRANS_UNIT];
  const uint8_t n = p.writeTo(wire);
  EXPECT_EQ(n, 5);  // header + path_len + payload

  Packet q;
  ASSERT_TRUE(q.readFrom(wire, n));
  EXPECT_EQ(q.getPayloadType(), PAYLOAD_TYPE_TRACE);
  EXPECT_EQ(q.payload_len, 3);
}
