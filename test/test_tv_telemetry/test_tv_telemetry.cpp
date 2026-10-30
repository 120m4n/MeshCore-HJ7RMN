#include <gtest/gtest.h>
#include <math.h>
#include "../../src/helpers/tv_telemetry.h"

// tv_sensor.cpp no entra en native: stubs con sensor fijo.
namespace tv {
uint8_t kind = 1;  // BME280
bool sensor_ready() { return true; }
int16_t read_temp_dC() { return 0; }
uint8_t read_hum_pct() { return 0; }
const char* sensor_kind() { return "test"; }
uint8_t sensor_kind_id() { return kind; }
}

class TvTelemetry : public ::testing::Test {
protected:
  void SetUp() override {
    memset(tv::ring, 0, sizeof(tv::ring));
    tv::kind = 1;
  }
};

TEST_F(TvTelemetry, EncodesV3HeaderAnchorDeltasGapAndEscape) {
  // anchor 07:13 UTC (fuera de bucket), +1 slot con delta chico,
  // hueco de 2 slots y un salto de T que no cabe en 1 char -> '!'
  tv::put(29840113, 330, 65);
  tv::put(29840130, 318, 68);   // dT=-12 dH=+3
  tv::put(29840220, 505, 73);   // 2 slots vacíos, dT=+187 -> escape
  char out[64];
  size_t n = tv::encode(29840220, 0, out, sizeof(out));

  // N = (3<<2)|1 BME280, AD = 3 registros
  EXPECT_STREQ("NADBx1LxKUBBXG.C!PyBJ", out);
  EXPECT_EQ(strlen(out), n);
}

TEST_F(TvTelemetry, BMP280HeaderIsO) {
  tv::kind = 2;
  tv::put(1000, 200, 180);
  char out[64];
  tv::encode(1000, 0, out, sizeof(out));
  EXPECT_EQ('O', out[0]);
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
  uint32_t now_min = 100020;
  tv::put(now_min - 1440, 111, 40);  // exactly 1 day old: excluded (< 1440 required, not <=)
  tv::put(now_min - 90, 222, 45);    // fresh: only record
  char out[64];
  tv::encode(now_min, 0, out, sizeof(out));
  EXPECT_STREQ("NABAAYZaG8At", out);
}

TEST_F(TvTelemetry, TruncatesWholeRecordsAndFixesCount) {
  // 48 registros con escape '!' (5 chars c/u) no entran en 32: el header
  // cuenta solo los que entraron y nunca se corta un registro a la mitad.
  for (uint32_t i = 0; i < tv::SLOTS; i++)
    tv::put(200010 + i * tv::INTERVAL_MIN, (int16_t)((i % 2) ? 500 : -500), 50);
  char out[64];
  size_t n = tv::encode(200010 + (tv::SLOTS - 1) * tv::INTERVAL_MIN, 0, out, 32);
  EXPECT_STREQ("NAEAAw1KPnAy!PoAy!PnAy!PoAy", out);  // AE = 4 registros
  EXPECT_EQ(strlen(out), n);
  EXPECT_LT(n, 32u);
}

TEST_F(TvTelemetry, FullStableDayFitsOneReply) {
  uint32_t now_min = 500010 - (500010 % tv::INTERVAL_MIN);
  for (uint32_t i = 0; i < tv::SLOTS; i++)
    tv::put(now_min - (tv::SLOTS - 1 - i) * tv::INTERVAL_MIN, 245, 180);
  char out[150];
  size_t n = tv::encode(now_min, 0, out, sizeof(out));
  EXPECT_EQ(0, strncmp("NAw", out, 3));      // 48 registros, el más viejo primero
  EXPECT_EQ(12u + 2u * (tv::SLOTS - 1), n);  // anchor + 47 deltas de 2 chars
}

TEST_F(TvTelemetry, HandleTvParsesSinceAndEncodes) {
  tv::put(9000, 10, 20);
  char reply[64];
  tv::handle_tv("0", 9000, reply, sizeof(reply));
  EXPECT_STREQ("NABAACMoAUAU", reply);
  tv::handle_tv("9000", 9000, reply, sizeof(reply));
  EXPECT_STREQ("-", reply);
}

TEST_F(TvTelemetry, HpaCodeCoversSeaLevelToHighAltitude) {
  EXPECT_EQ(180, tv::hpa_code(905.0f));    // Bucaramanga
  EXPECT_EQ(104, tv::hpa_code(752.0f));    // Bogotá
  EXPECT_EQ(234, tv::hpa_code(1013.25f));  // nivel del mar
  EXPECT_EQ(0, tv::hpa_code(400.0f));
  EXPECT_EQ(255, tv::hpa_code(2000.0f));
  EXPECT_EQ(0, tv::hpa_code(NAN));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
