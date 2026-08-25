#include <gtest/gtest.h>
#include "helpers/i2c_sensors/AM2301Payload.h"

TEST(AM2301Payload, DecodesPositiveTemperatureAndHumidity) {
  // 23.5C, 45.2%, status OK
  uint8_t buf[5] = { AM2301_STATUS_OK, 0x00, 0xEB, 0x01, 0xC4 };
  float temp_c, hum_pct;
  uint8_t status;

  decodeAM2301Payload(buf, &temp_c, &hum_pct, &status);

  EXPECT_EQ(AM2301_STATUS_OK, status);
  EXPECT_FLOAT_EQ(23.5f, temp_c);
  EXPECT_FLOAT_EQ(45.2f, hum_pct);
}

TEST(AM2301Payload, DecodesNegativeTemperature) {
  // -5.0C -> -50 as int16_t big-endian (0xFFCE), 60.0% humidity, status OK
  uint8_t buf[5] = { AM2301_STATUS_OK, 0xFF, 0xCE, 0x02, 0x58 };
  float temp_c, hum_pct;
  uint8_t status;

  decodeAM2301Payload(buf, &temp_c, &hum_pct, &status);

  EXPECT_EQ(AM2301_STATUS_OK, status);
  EXPECT_FLOAT_EQ(-5.0f, temp_c);
  EXPECT_FLOAT_EQ(60.0f, hum_pct);
}

TEST(AM2301Payload, DecodesCachedStatus) {
  uint8_t buf[5] = { AM2301_STATUS_CACHED, 0x00, 0x64, 0x01, 0x2C };
  float temp_c, hum_pct;
  uint8_t status;

  decodeAM2301Payload(buf, &temp_c, &hum_pct, &status);

  EXPECT_EQ(AM2301_STATUS_CACHED, status);
  EXPECT_FLOAT_EQ(10.0f, temp_c);
  EXPECT_FLOAT_EQ(30.0f, hum_pct);
}

TEST(AM2301Payload, DecodesNoReadingYetStatus) {
  uint8_t buf[5] = { AM2301_STATUS_NO_READING_YET, 0x00, 0x00, 0x00, 0x00 };
  float temp_c, hum_pct;
  uint8_t status;

  decodeAM2301Payload(buf, &temp_c, &hum_pct, &status);

  EXPECT_EQ(AM2301_STATUS_NO_READING_YET, status);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
