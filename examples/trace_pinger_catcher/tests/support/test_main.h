#pragma once
// main() para los tests nativos (PlatformIO + googletest). Se incluye una vez por suite.
#include <gtest/gtest.h>
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
