#include <gtest/gtest.h>

#include <cstring>

#include "tracecore/Config.h"

using namespace tracecore;

TEST(Config, SetValidAndInvalid) {
  PingerConfig c;
  EXPECT_TRUE(applySet(c, "freq", "910.525").result == SetResult::Ok);
  EXPECT_EQ(c.radio.freq_khz, 910525u);
  EXPECT_TRUE(applySet(c, "BW", "62.5").result == SetResult::Ok);  // clave sin distinguir mayúsculas
  EXPECT_EQ(c.radio.bw_x100, 6250);
  EXPECT_TRUE(applySet(c, "sf", "7").result == SetResult::Ok);
  EXPECT_TRUE(applySet(c, "hash", "a1").result == SetResult::Ok);
  EXPECT_EQ(*c.hash, 0xA1);
  EXPECT_TRUE(applySet(c, "timeout", "auto").result == SetResult::Ok);
  EXPECT_TRUE(applySet(c, "raw", "1").result == SetResult::Ok);

  const PingerConfig before = c;
  auto o = applySet(c, "sf", "13");
  EXPECT_TRUE(o.result == SetResult::Range);
  EXPECT_STREQ(o.detail, "sf 5-12");
  o = applySet(c, "rate", "200");
  EXPECT_STREQ(o.detail, "rate>=1000");
  EXPECT_TRUE(applySet(c, "bw", "63").result == SetResult::Range);
  EXPECT_TRUE(applySet(c, "txp", "23").result == SetResult::Range);
  EXPECT_TRUE(applySet(c, "hash", "A").result == SetResult::Range);
  EXPECT_TRUE(applySet(c, "sf", "x").result == SetResult::Syntax);
  EXPECT_TRUE(applySet(c, "nope", "1").result == SetResult::Unknown);
  EXPECT_EQ(c.radio.sf, before.radio.sf);  // un SET inválido no cambia nada
  EXPECT_EQ(c.rate_ms, before.rate_ms);
}

TEST(Config, TxpAcceptsFullRangeUpTo22) {
  PingerConfig c;
  EXPECT_TRUE(applySet(c, "txp", "22").result == SetResult::Ok);
  EXPECT_TRUE(applySet(c, "txp", "-9").result == SetResult::Ok);
  EXPECT_EQ(c.radio.txp_dbm, -9);
}

TEST(Config, RadioChangedFlag) {
  PingerConfig c;
  EXPECT_TRUE(applySet(c, "freq", "915.000").radio_changed);
  EXPECT_FALSE(applySet(c, "rate", "2000").radio_changed);
  EXPECT_TRUE(isRadioKey("TXP"));
  EXPECT_FALSE(isRadioKey("hash"));
}

TEST(Config, FormatCfgDefaults) {
  PingerConfig c;
  char b[160];
  formatCfg(b, sizeof b, c);
  EXPECT_STREQ(b, "#CFG freq=915.800 bw=250 sf=10 cr=5 txp=22 hash= rate=1500 timeout=auto raw=0");
  c.hash = 0xA1;
  c.radio.bw_x100 = 6250;
  c.timeout_ms = 900;
  formatCfg(b, sizeof b, c);
  EXPECT_STREQ(b, "#CFG freq=915.800 bw=62.5 sf=10 cr=5 txp=22 hash=A1 rate=1500 timeout=900 raw=0");
}

TEST(Config, SerializeRoundtripAndCorruption) {
  PingerConfig c;
  c.hash = 0x5C;
  c.radio.sf = 8;
  c.radio.txp_dbm = -3;
  c.timeout_ms = 1234;
  c.raw = true;
  uint8_t blob[kCfgBlobSize];
  EXPECT_EQ(serialize(c, blob), kCfgBlobSize);
  auto d = deserialize(blob, sizeof blob);
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->radio.sf, 8);
  EXPECT_EQ(d->radio.txp_dbm, -3);
  EXPECT_EQ(*d->hash, 0x5C);
  EXPECT_EQ(*d->timeout_ms, 1234u);
  EXPECT_TRUE(d->raw);

  uint8_t bad[kCfgBlobSize];
  std::memcpy(bad, blob, sizeof bad);
  bad[12] ^= 0x01;  // CRC roto
  EXPECT_FALSE(deserialize(bad, sizeof bad).has_value());
  EXPECT_FALSE(deserialize(blob, sizeof blob - 1).has_value());

  PingerConfig inv;
  inv.radio.sf = 13;  // CRC correcto pero valor fuera de rango
  serialize(inv, blob);
  EXPECT_FALSE(deserialize(blob, sizeof blob).has_value());
}

TEST(Config, NoHashByDefault) {
  PingerConfig c;
  EXPECT_FALSE(c.hash.has_value());
}

#include "../support/test_main.h"
