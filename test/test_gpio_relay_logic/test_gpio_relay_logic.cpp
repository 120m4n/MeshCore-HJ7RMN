#include <gtest/gtest.h>
#include "helpers/actuators/GPIORelayLogic.h"

TEST(RelayLogicalToPhysicalHigh, ActiveHighOnDrivesHigh) {
  EXPECT_TRUE(relayLogicalToPhysicalHigh(true, false));
}

TEST(RelayLogicalToPhysicalHigh, ActiveHighOffDrivesLow) {
  EXPECT_FALSE(relayLogicalToPhysicalHigh(false, false));
}

TEST(RelayLogicalToPhysicalHigh, ActiveLowOnDrivesLow) {
  EXPECT_FALSE(relayLogicalToPhysicalHigh(true, true));
}

TEST(RelayLogicalToPhysicalHigh, ActiveLowOffDrivesHigh) {
  EXPECT_TRUE(relayLogicalToPhysicalHigh(false, true));
}

TEST(BuildRelayStateBits, RendersChannelsZeroAndOneOn) {
  char out[5];
  buildRelayStateBits(0b0011, out);
  EXPECT_STREQ("1100", out);
}

TEST(BuildRelayStateBits, RendersChannelsTwoAndThreeOn) {
  char out[5];
  buildRelayStateBits(0b1100, out);
  EXPECT_STREQ("0011", out);
}

TEST(BuildRelayStateBits, RendersAllOff) {
  char out[5];
  buildRelayStateBits(0, out);
  EXPECT_STREQ("0000", out);
}

TEST(BuildRelayStateBits, RendersAllOn) {
  char out[5];
  buildRelayStateBits(0b1111, out);
  EXPECT_STREQ("1111", out);
}

TEST(ParseRelayActuatorCmd, MatchesOnCommandAsSuffix) {
  uint8_t pin; bool state;
  ASSERT_TRUE(parseRelayActuatorCmd("Node1: PIN2_ON", "PIN", "_ON", "_OFF", &pin, &state));
  EXPECT_EQ(2, pin);
  EXPECT_TRUE(state);
}

TEST(ParseRelayActuatorCmd, MatchesOffCommandAsSuffix) {
  uint8_t pin; bool state;
  ASSERT_TRUE(parseRelayActuatorCmd("Node1: PIN0_OFF", "PIN", "_ON", "_OFF", &pin, &state));
  EXPECT_EQ(0, pin);
  EXPECT_FALSE(state);
}

TEST(ParseRelayActuatorCmd, RejectsDigitAboveMaxPin) {
  uint8_t pin; bool state;
  EXPECT_FALSE(parseRelayActuatorCmd("Node1: PIN4_ON", "PIN", "_ON", "_OFF", &pin, &state));
}

TEST(ParseRelayActuatorCmd, RejectsWrongPrefix) {
  uint8_t pin; bool state;
  EXPECT_FALSE(parseRelayActuatorCmd("Node1: LED2_ON", "PIN", "_ON", "_OFF", &pin, &state));
}

TEST(ParseRelayActuatorCmd, RejectsTextShorterThanCommand) {
  uint8_t pin; bool state;
  EXPECT_FALSE(parseRelayActuatorCmd("ON", "PIN", "_ON", "_OFF", &pin, &state));
}

TEST(RelayTextEndsWithCmd, MatchesStatusCommandAsSuffix) {
  EXPECT_TRUE(relayTextEndsWithCmd("Node1: PIN_STATUS", "PIN_STATUS"));
}

TEST(RelayTextEndsWithCmd, RejectsNonMatchingSuffix) {
  EXPECT_FALSE(relayTextEndsWithCmd("Node1: PIN0_ON", "PIN_STATUS"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
