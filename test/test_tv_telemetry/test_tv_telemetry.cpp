#include <gtest/gtest.h>
#include "../../src/helpers/tv_telemetry.h"

class TvTelemetry : public ::testing::Test {
protected:
  void SetUp() override {
    memset(tv::ring, 0, sizeof(tv::ring));
  }
};

TEST_F(TvTelemetry, MatchesSpecReferenceVector) {
  // Ejemplo de referencia de la spec: anchor 07:13 UTC, epoch_min=29840113
  tv::put(29840113, 330, 65);
  tv::put(29840174, 318, 68);   // dT=-12 dH=+3 dt=61
  tv::put(29840189, 305, 73);   // dT=-25 dH=+8 dt=76

  char out[64];
  size_t n = tv::encode(29840189, 0, out, sizeof(out));

  EXPECT_STREQ("330,65,29840113;-12,3,61;-25,8,76", out);
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, NoNewDataReturnsDash) {
  tv::put(1000, 200, 50);

  char out[64];
  size_t n = tv::encode(1000, 1000, out, sizeof(out));  // since == last epoch

  EXPECT_STREQ("-", out);
  EXPECT_EQ(1u, n);
}

TEST_F(TvTelemetry, EmptyRingReturnsDash) {
  char out[64];
  size_t n = tv::encode(5000, 0, out, sizeof(out));

  EXPECT_STREQ("-", out);
  EXPECT_EQ(1u, n);
}

TEST_F(TvTelemetry, CapBelow32ReturnsZeroWithoutTouchingBuffer) {
  tv::put(1000, 200, 50);

  char out[8] = {0};
  size_t n = tv::encode(1000, 0, out, 10);  // cap < 32 short-circuits before writing anything

  EXPECT_EQ(0u, n);
  EXPECT_STREQ("", out);  // untouched: caller must not send this as a reply
}

TEST_F(TvTelemetry, StaleSlotOlderThanADayIsExcluded) {
  uint32_t now_min = 100000;
  tv::put(now_min - 1440, 111, 40);  // exactly 1 day old: excluded (< 1440 required, not <=)
  tv::put(now_min - 100, 222, 45);   // fresh: included

  char out[64];
  size_t n = tv::encode(now_min, 0, out, sizeof(out));

  char expected[32];
  snprintf(expected, sizeof(expected), "222,45,%u", (unsigned)(now_min - 100));
  EXPECT_STREQ(expected, out);
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, TruncatesCleanlyWhenPacketIsFull) {
  tv::put(200000, 100, 50);       // anchor: "100,50,200000" (13 bytes)
  tv::put(200015, 105, 51);       // delta:  ";5,1,15" (7 bytes) -> cumulative 20, fits cap=32
  tv::put(200030, -12345, 52);    // delta:  ";-12450,1,15" (12 bytes) -> cumulative 32, overflows cap=32

  char out[64];
  size_t n = tv::encode(200030, 0, out, 32);  // cap is the encoder's documented minimum

  EXPECT_STREQ("100,50,200000;5,1,15", out);  // third record excluded whole, never split
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, FullRingEncodesOldestValidSlotsFirst) {
  // Llena las 96 slots; encode() debe recorrerlas en orden cronológico
  // ascendente empezando por la más vieja, sin depender del orden de escritura.
  uint32_t now_min = 500000 - (500000 % tv::INTERVAL_MIN);  // alineado a slot
  for (uint32_t i = 0; i < tv::SLOTS; i++) {
    uint32_t epoch = now_min - (tv::SLOTS - 1 - i) * tv::INTERVAL_MIN;
    tv::put(epoch, (int16_t)(200 + i), (uint8_t)(40 + (i % 50)));
  }

  char out[128];
  size_t n = tv::encode(now_min, 0, out, sizeof(out));

  uint32_t oldest_epoch = now_min - (tv::SLOTS - 1) * tv::INTERVAL_MIN;
  char expected_prefix[24];
  snprintf(expected_prefix, sizeof(expected_prefix), "200,40,%u", (unsigned)oldest_epoch);
  EXPECT_EQ(0, strncmp(expected_prefix, out, strlen(expected_prefix)));  // oldest slot leads
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, HandleTvParsesSinceAndEncodes) {
  tv::put(9000, 10, 20);

  char reply[64];
  tv::handle_tv("0", 9000, reply, sizeof(reply));
  EXPECT_STREQ("10,20,9000", reply);

  tv::handle_tv("9000", 9000, reply, sizeof(reply));
  EXPECT_STREQ("-", reply);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
