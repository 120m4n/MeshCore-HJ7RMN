// US2-AS1..AS5: modelo ejecutable de la decisión TRACE del repetidor (Mesh.cpp L42-67),
// probado contra las tramas reales de tracecore.
#include <Packet.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <random>

#include "RepeaterModel.h"
#include "tracecore/TraceFrame.h"

using mesh::Packet;
using tracecore_test::RepeaterAction;
using tracecore_test::RepeaterModel;

static RepeaterModel repeaterWithHash(uint8_t h) {
  RepeaterModel r;
  r.pub_key[0] = h;
  return r;
}

static Packet requestPacket(uint32_t tag, uint8_t hash) {
  uint8_t req[tracecore::kRequestLen];
  tracecore::buildRequest(tag, hash, req);
  Packet p;
  EXPECT_TRUE(p.readFrom(req, sizeof req));
  return p;
}

TEST(RepeaterModel, MatchingRepeaterRetransmitsAndAppendsSnr) {  // AS1
  RepeaterModel r = repeaterWithHash(0xA1);
  Packet p = requestPacket(0x78563412, 0xA1);
  EXPECT_TRUE(r.onRecv(p, -6.3f) == RepeaterAction::Retransmit);
  EXPECT_EQ(p.path_len, 1);
  EXPECT_EQ((int8_t)p.path[0], -25);  // truncamiento hacia cero: -6.3 × 4 = -25.2 -> -25
}

TEST(RepeaterModel, NonMatchingRepeaterReleases) {  // AS2
  RepeaterModel r = repeaterWithHash(0xB2);
  Packet p = requestPacket(0x78563412, 0xA1);
  EXPECT_TRUE(r.onRecv(p, -6.3f) == RepeaterAction::Release);
  EXPECT_EQ(p.path_len, 0);
}

TEST(RepeaterModel, EchoIsNotPropagated) {  // AS3
  Packet p = requestPacket(0x78563412, 0xA1);
  RepeaterModel first = repeaterWithHash(0xA1);
  ASSERT_TRUE(first.onRecv(p, -6.3f) == RepeaterAction::Retransmit);
  for (uint8_t h : {0xA1, 0xB2, 0x00}) {  // cualquier repetidor, incluido el mismo
    RepeaterModel other = repeaterWithHash(h);
    Packet echo = p;
    EXPECT_TRUE(other.onRecv(echo, 3.0f) == RepeaterAction::TraceComplete);
    EXPECT_EQ(echo.path_len, 1);  // no muta
  }
}

TEST(RepeaterModel, SameTagTwiceIsDroppedByTheSeenTable) {  // AS4: justifica FR-002 de la fase 001
  RepeaterModel r = repeaterWithHash(0xA1);
  Packet a = requestPacket(0x11223344, 0xA1), b = requestPacket(0x11223344, 0xA1);
  EXPECT_TRUE(r.onRecv(a, -5.0f) == RepeaterAction::Retransmit);
  EXPECT_TRUE(r.onRecv(b, -5.0f) == RepeaterAction::Release);
  Packet c = requestPacket(0x11223345, 0xA1);  // otro tag: pasa
  EXPECT_TRUE(r.onRecv(c, -5.0f) == RepeaterAction::Retransmit);
}

TEST(RepeaterModel, FullRoundTripRecoversInjectedSnr) {  // AS5: pinger -> repetidor -> pinger
  std::mt19937 rng(0x59414749);
  for (int i = 0; i < 2000; i++) {
    const uint32_t tag = rng();
    const uint8_t hash = (uint8_t)rng();
    const float snr = -32.0f + (rng() % 256) * 0.25f;  // [-32, +31.75]
    RepeaterModel r = repeaterWithHash(hash);
    Packet p = requestPacket(tag, hash);
    ASSERT_TRUE(r.onRecv(p, snr) == RepeaterAction::Retransmit);
    uint8_t wire[MAX_TRANS_UNIT];
    const uint8_t n = p.writeTo(wire);
    const auto e = tracecore::parseEcho(wire, n, hash);
    const bool good = e.has_value() && e->tag == tag && e->snr_up_q == (int8_t)(snr * 4);
    EXPECT_TRUE(good);
    if (!good) { std::printf("  seed=0x59414749 iter=%d snr=%f\n", i, snr); return; }
  }
}

TEST(RepeaterModel, SnrQuantisationTruncatesTowardZero) {
  RepeaterModel r = repeaterWithHash(0xA1);
  Packet a = requestPacket(1, 0xA1);
  r.onRecv(a, 7.99f);
  EXPECT_EQ((int8_t)a.path[0], 31);  // 31.96 -> 31, no redondeo
  Packet b = requestPacket(2, 0xA1);
  r.onRecv(b, -0.1f);
  EXPECT_EQ((int8_t)b.path[0], 0);   // -0.4 -> 0
}

TEST(RepeaterModel, DisableFwdReleases) {
  RepeaterModel r = repeaterWithHash(0xA1);
  r.disable_fwd = true;
  Packet p = requestPacket(0x01020304, 0xA1);
  EXPECT_TRUE(r.onRecv(p, -6.0f) == RepeaterAction::Release);
  EXPECT_EQ(p.path_len, 0);
}

TEST(RepeaterModel, OnlyDirectTraceIsHandled) {
  RepeaterModel r = repeaterWithHash(0xA1);
  Packet flood = requestPacket(5, 0xA1);
  flood.header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;
  EXPECT_TRUE(r.onRecv(flood, -6.0f) == RepeaterAction::Release);
  Packet advert = requestPacket(6, 0xA1);
  advert.header = (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
  EXPECT_TRUE(r.onRecv(advert, -6.0f) == RepeaterAction::Release);
}
