#include <gtest/gtest.h>

#include <vector>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

static std::vector<uint8_t> copy(const Action& a) { return std::vector<uint8_t>(a.bytes, a.bytes + a.len); }

TEST(PingSingle, WithoutHashIsRejected) {
  Rig r(false);
  EXPECT_EQ(r.p->onCommand("PING", 0).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR state nohash");
  r.p->onCommand("GO", 0);
  EXPECT_EQ(r.out.lines.back(), "!ERR state nohash");
}

TEST(PingSingle, TransmitsContractFrameThenListens) {
  Rig r;
  Action a = r.p->onCommand("PING", 1000);
  ASSERT_EQ(a.kind, Action::Transmit);
  const std::vector<uint8_t> want = {0x26, 0x00, 0x11, 0x11, 0x11, 0x11, 0, 0, 0, 0, 0x00, 0xA1};
  EXPECT_TRUE(copy(a) == want);
  EXPECT_EQ(r.p->onTxDone(1177, true).kind, Action::StartRx);
}

TEST(PingSingle, EchoProducesOkLineAtDeadline) {
  Rig r;
  Action a = r.p->onCommand("PING", 1000);
  const auto req = copy(a);
  r.p->onTxDone(1177, true);  // deadline = 1177 + 1207 (timeout auto del preset por defecto)
  const auto echo = echoFor(req.data(), -25);
  r.p->onRx(echo.data(), echo.size(), -104.0f, -1.5f, 1477);  // rtt = 300
  EXPECT_EQ(r.p->tick(2383).kind, Action::None);
  EXPECT_EQ(r.out.count("P,"), 0u);
  EXPECT_EQ(r.p->tick(2384).kind, Action::Standby);
  EXPECT_EQ(r.out.lines.back(), "P,1,11111111,ok,300,-6.25,-1.50,-104.0,1");
}

TEST(PingSingle, NoEchoMeansTimeout) {
  Rig r;
  r.p->onCommand("PING", 0);
  r.p->onTxDone(177, true);
  r.p->tick(1384);
  EXPECT_EQ(r.out.lines.back(), "P,1,11111111,timeout,,,,,0");
  EXPECT_EQ(r.p->onCommand("PING", 1400).kind, Action::Transmit);  // vuelve a quedar disponible
}

TEST(PingSingle, ForeignTagIsIgnored) {
  Rig r;
  const auto req = copy(r.p->onCommand("PING", 0));
  r.p->onTxDone(177, true);
  auto other = echoFor(req.data(), -25);
  other[3] ^= 0xFF;  // otro tag
  r.p->onRx(other.data(), other.size(), -100.f, 0.f, 400);
  r.p->tick(1384);
  EXPECT_EQ(r.out.lines.back(), "P,1,11111111,timeout,,,,,0");
}

TEST(PingSingle, ConsecutivePingsUseDifferentTagsEvenIfRngRepeats) {
  Rig r;  // FakeRng devuelve siempre 0x11111111
  r.p->onCommand("PING", 0);
  r.p->onTxDone(177, true);
  r.p->tick(1384);
  const auto second = copy(r.p->onCommand("PING", 2000));
  r.p->onTxDone(2177, true);
  r.p->tick(3384);
  EXPECT_EQ(r.out.lines.back(), "P,2,11111112,timeout,,,,,0");
  EXPECT_EQ(second[2], 0x12);
}

TEST(PingSingle, ChannelBusyThreeTimesReportsBusy) {
  Rig r;
  ASSERT_EQ(r.p->onCommand("PING", 0).kind, Action::Transmit);
  EXPECT_EQ(r.p->onChannelBusy(10).kind, Action::None);
  // backoff = (rng % 3 + 1) × 120 ms; con rng=0x11111111, 0x11111111 % 3 = 2 -> 360 ms
  EXPECT_EQ(r.p->tick(369).kind, Action::None);
  const Action retry = r.p->tick(370);
  ASSERT_EQ(retry.kind, Action::Transmit);
  EXPECT_EQ(retry.bytes[2], 0x11);  // mismo ping, mismo tag
  EXPECT_EQ(r.p->onChannelBusy(380).kind, Action::None);
  ASSERT_EQ(r.p->tick(740).kind, Action::Transmit);
  EXPECT_EQ(r.p->onChannelBusy(750).kind, Action::Standby);
  EXPECT_EQ(r.out.lines.back(), "P,1,11111111,busy,,,,,0");
}

TEST(PingSingle, TxErrorIsReported) {
  Rig r;
  r.p->onCommand("PING", 0);
  r.p->onTxDone(50, false);
  EXPECT_EQ(r.out.lines.back(), "P,1,11111111,txerr,,,,,0");
}
