/**
 * @file port_without_delay.cpp
 * @brief Must NOT compile: the power-up time after the reset at start-up needs the port's
 * DelayUs().
 *
 * EXPECT: DelayUs
 */
#include "ads7952.hpp"
#include "mock_ads7952.hpp"

class NoDelayPort : public ads7952_test::MockPortBase<NoDelayPort> {};

int main() {
  NoDelayPort port;
  ads7952::ADS7952<NoDelayPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
