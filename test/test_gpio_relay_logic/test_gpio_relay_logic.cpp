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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
