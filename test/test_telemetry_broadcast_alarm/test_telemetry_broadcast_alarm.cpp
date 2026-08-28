#include <gtest/gtest.h>
#include "../../examples/companion_radio/TelemetryBroadcastLogic.h"

TEST(ExtractTempHumidity, FindsBothValuesAmongOtherReadings) {
  LppReading readings[] = {
    { 0x02 /* some other LPP type */, 3.3f },
    { LPP_TYPE_TEMPERATURE, 23.5f },
    { LPP_TYPE_RELATIVE_HUMIDITY, 45.2f },
  };
  float temp_c = 0, hum_pct = 0;
  ASSERT_TRUE(extractTempHumidity(readings, 3, &temp_c, &hum_pct));
  EXPECT_FLOAT_EQ(23.5f, temp_c);
  EXPECT_FLOAT_EQ(45.2f, hum_pct);
}

TEST(ExtractTempHumidity, FailsWhenTemperatureMissing) {
  LppReading readings[] = { { LPP_TYPE_RELATIVE_HUMIDITY, 45.2f } };
  float temp_c = 0, hum_pct = 0;
  EXPECT_FALSE(extractTempHumidity(readings, 1, &temp_c, &hum_pct));
}

TEST(ExtractTempHumidity, FailsWhenHumidityMissing) {
  LppReading readings[] = { { LPP_TYPE_TEMPERATURE, 23.5f } };
  float temp_c = 0, hum_pct = 0;
  EXPECT_FALSE(extractTempHumidity(readings, 1, &temp_c, &hum_pct));
}

TEST(ExtractTempHumidity, FailsOnEmptyReadings) {
  float temp_c = 0, hum_pct = 0;
  EXPECT_FALSE(extractTempHumidity(nullptr, 0, &temp_c, &hum_pct));
}

TEST(AlarmTransition, FiresOnceWhenCrossingAboveThreshold) {
  bool active = false;
  EXPECT_EQ(AlarmAction::FIRE, checkAlarmTransition(47.2f, 45.0f, 2.0f, &active));
  EXPECT_TRUE(active);
}

TEST(AlarmTransition, StaysSilentWhileSustainedAboveThreshold) {
  bool active = true; // already firing
  EXPECT_EQ(AlarmAction::NONE, checkAlarmTransition(48.0f, 45.0f, 2.0f, &active));
  EXPECT_TRUE(active);
}

TEST(AlarmTransition, StaysSilentInHysteresisBand) {
  bool active = true; // already firing, threshold 45, hysteresis 2 -> band is (43, 45]
  EXPECT_EQ(AlarmAction::NONE, checkAlarmTransition(44.0f, 45.0f, 2.0f, &active));
  EXPECT_TRUE(active);
}

TEST(AlarmTransition, RecoversWhenDroppingBelowHysteresisBand) {
  bool active = true;
  EXPECT_EQ(AlarmAction::RECOVER, checkAlarmTransition(42.8f, 45.0f, 2.0f, &active));
  EXPECT_FALSE(active);
}

TEST(AlarmTransition, StaysSilentWhileNormalAndBelowThreshold) {
  bool active = false;
  EXPECT_EQ(AlarmAction::NONE, checkAlarmTransition(20.0f, 45.0f, 2.0f, &active));
  EXPECT_FALSE(active);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
