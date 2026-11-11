#include <gtest/gtest.h>

#include <vector>

#include "../support/Fakes.h"

using namespace tracecore;
using namespace fakes;

TEST(Monitor, MonOnLogsEveryPacketAndNeverTransmits) {
  Rig r;
  EXPECT_EQ(r.p->onCommand("MON on", 0).kind, Action::StartRx);
  EXPECT_EQ(r.out.lines.back(), "#OK mon=on");
  const uint8_t advert[] = {0x11, 0x02, 0xAA};
  r.p->onRx(advert, sizeof advert, -108.0f, -4.5f, 123456);
  EXPECT_EQ(r.out.lines.back(), "R,123456,-108.0,-4.50,1102AA");
  EXPECT_EQ(r.p->onCommand("PING", 1).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR state mon");
  r.p->onCommand("GO", 1);
  EXPECT_EQ(r.out.lines.back(), "!ERR state mon");
  EXPECT_EQ(r.p->onCommand("MON off", 2).kind, Action::Standby);
  EXPECT_EQ(r.p->onCommand("PING", 3).kind, Action::Transmit);
}

TEST(Monitor, MonOnWhilePingInFlightIsBusy) {
  Rig r;
  r.p->onCommand("PING", 0);
  EXPECT_EQ(r.p->onCommand("MON on", 1).kind, Action::None);
  EXPECT_EQ(r.out.lines.back(), "!ERR state busy");
}

TEST(Monitor, RawEmitsAcceptedEchoBesidesPing) {
  Rig r;
  r.p->onCommand("SET raw=1", 0);
  Sim s(*r.p);
  s.cmd("PING");
  s.run(1500);
  EXPECT_EQ(r.out.count("R,"), 1u);
  EXPECT_EQ(r.out.count("P,"), 1u);
  EXPECT_EQ(r.out.first("R,").substr(r.out.first("R,").find(',', 2)), ",-104.0,-1.50,2601E711111111000000000" "0A1");
}

TEST(Monitor, TwoEchoesSameTagReportCollision) {
  Rig r;
  const auto a = r.p->onCommand("PING", 0);
  const std::vector<uint8_t> req(a.bytes, a.bytes + a.len);
  r.p->onTxDone(177, true);
  const auto e1 = echoFor(req.data(), -25);
  const auto e2 = echoFor(req.data(), -10);
  r.p->onRx(e1.data(), e1.size(), -104.f, -1.5f, 400);
  r.p->onRx(e2.data(), e2.size(), -90.f, 2.0f, 450);
  r.p->tick(1384);
  EXPECT_EQ(r.out.lines[r.out.lines.size() - 2], "P,1,11111111,ok,223,-6.25,-1.50,-104.0,2");  // usa el primero
  EXPECT_EQ(r.out.lines.back(), "#WARN hash_collision");
}

TEST(Monitor, LateEchoOnlyLogsRaw) {
  Rig r;
  r.p->onCommand("SET raw=1", 0);
  const auto a = r.p->onCommand("PING", 0);
  const std::vector<uint8_t> req(a.bytes, a.bytes + a.len);
  r.p->onTxDone(177, true);
  r.p->tick(1384);  // timeout
  const size_t before = r.out.lines.size();
  const auto late = echoFor(req.data(), -25);
  r.p->onRx(late.data(), late.size(), -104.f, -1.5f, 1500);
  ASSERT_EQ(r.out.lines.size(), before + 1);
  EXPECT_EQ(r.out.lines.back().substr(0, 2), "R,");
  EXPECT_EQ(r.out.count("P,"), 1u);
}

TEST(Monitor, LateEchoWithoutRawIsSilent) {
  Rig r;
  const auto a = r.p->onCommand("PING", 0);
  const std::vector<uint8_t> req(a.bytes, a.bytes + a.len);
  r.p->onTxDone(177, true);
  r.p->tick(1384);
  const size_t before = r.out.lines.size();
  const auto late = echoFor(req.data(), -25);
  r.p->onRx(late.data(), late.size(), -104.f, -1.5f, 1500);
  EXPECT_EQ(r.out.lines.size(), before);
}
