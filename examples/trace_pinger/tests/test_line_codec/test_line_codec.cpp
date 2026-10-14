#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "tracecore/LineCodec.h"

using namespace tracecore;
using K = Command::Kind;

static std::string ping(const PingResult& r) { char b[kMaxLine]; formatPing(b, sizeof b, r); return b; }
static std::string summary(const BurstSummary& s) { char b[kMaxLine]; formatSummary(b, sizeof b, s); return b; }

TEST(LineCodec, FormatPingOk) {
  PingResult r;
  r.seq = 17; r.tag = 0x78563412; r.status = Status::Ok; r.rtt_ms = 412;
  r.snr_up_c = -625; r.snr_dn_c = -450; r.rssi_dn_d = -1080; r.n_resp = 1;
  EXPECT_EQ(ping(r), "P,17,78563412,ok,412,-6.25,-4.50,-108.0,1");
  r.snr_up_c = 325; r.snr_dn_c = 0;
  EXPECT_EQ(ping(r), "P,17,78563412,ok,412,+3.25,+0.00,-108.0,1");
}

TEST(LineCodec, FormatPingNonOk) {
  PingResult r;
  r.seq = 18; r.tag = 0x0A1B2C3D; r.status = Status::Timeout;
  EXPECT_EQ(ping(r), "P,18,0A1B2C3D,timeout,,,,,0");
  r.status = Status::Busy;
  EXPECT_EQ(ping(r), "P,18,0A1B2C3D,busy,,,,,0");
}

TEST(LineCodec, FormatSummary) {
  BurstSummary s;
  s.n = 5; s.ok = 5; s.has_median = true; s.med_up_c = -625; s.med_dn_c = -450; s.med_rssi_d = -1080;
  EXPECT_EQ(summary(s), "S,5,5,0,-6.25,-4.50,-108.0,0");
  BurstSummary none;
  none.n = 5; none.timeouts = 5;
  EXPECT_EQ(summary(none), "S,5,0,100,,,,0");
  BurstSummary mixed;  // busy no cuenta como pérdida
  mixed.n = 5; mixed.ok = 3; mixed.timeouts = 1; mixed.busy = 1;
  mixed.has_median = true; mixed.med_up_c = 100; mixed.med_dn_c = 200; mixed.med_rssi_d = -900;
  EXPECT_EQ(summary(mixed), "S,5,3,25,+1.00,+2.00,-90.0,1");
  BurstSummary allbusy;
  allbusy.n = 2; allbusy.busy = 2;
  EXPECT_EQ(summary(allbusy), "S,2,0,,,,,2");
}

TEST(LineCodec, FormatRawAndTruncation) {
  const uint8_t f[] = {0x26, 0x01, 0xE7, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0xA1};
  char b[kMaxLine];
  formatRaw(b, sizeof b, 123456, -1080, -450, f, sizeof f);
  EXPECT_STREQ(b, "R,123456,-108.0,-4.50,2601E7123456780000000000A1");

  uint8_t big[255];
  std::memset(big, 0xAB, sizeof big);
  size_t n = formatRaw(b, sizeof b, 4294967295u, -1080, -450, big, sizeof big);
  EXPECT_LE(n, kMaxLine - 1);
  EXPECT_EQ(std::string(b).substr(std::string(b).size() - 2), "..");
}

TEST(LineCodec, ParseCommands) {
  EXPECT_TRUE(parseCommand("hello\r").kind == K::Hello);
  EXPECT_TRUE(parseCommand("CFG?").kind == K::CfgQuery);
  EXPECT_TRUE(parseCommand("  save ").kind == K::Save);
  EXPECT_TRUE(parseCommand("GO").kind == K::Go);
  EXPECT_TRUE(parseCommand("stop").kind == K::Stop);
  EXPECT_TRUE(parseCommand("").kind == K::Empty);
  EXPECT_TRUE(parseCommand("FOO").kind == K::Unknown);

  Command p = parseCommand("PING");
  EXPECT_TRUE(p.kind == K::Ping);
  EXPECT_FALSE(p.has_n);
  p = parseCommand("PING 5");
  EXPECT_TRUE(p.has_n);
  EXPECT_EQ(p.n, 5);
  EXPECT_TRUE(parseCommand("PING x").kind == K::Syntax);

  Command m = parseCommand("MON on");
  EXPECT_TRUE(m.kind == K::Mon && m.on);
  EXPECT_TRUE(parseCommand("mon OFF").kind == K::Mon);
  EXPECT_TRUE(parseCommand("MON maybe").kind == K::Syntax);

  Command s = parseCommand("SET Hash=A1");
  EXPECT_TRUE(s.kind == K::Set);
  EXPECT_STREQ(s.key, "Hash");
  EXPECT_STREQ(s.val, "A1");
  EXPECT_TRUE(parseCommand("SET hash").kind == K::Syntax);
  EXPECT_TRUE(parseCommand("SET =1").kind == K::Syntax);
}

#include "../support/test_main.h"
