/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the driver is constrained by the port concept.
 *
 * EXPECT: Port
 */
#include "ads7952.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  ads7952::ADS7952<NotAPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
