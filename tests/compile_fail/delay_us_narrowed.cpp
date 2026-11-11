/**
 * @file delay_us_narrowed.cpp
 * @brief Must NOT compile: a DelayUs hook with a narrower parameter would truncate long waits, so
 * the driver rejects it instead of using it (handbook KB-CPP20-007).
 *
 * EXPECT: DelayUs hook must be
 */
#include <cstdint>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"

class ShortDelayPort : public ads7952_test::MockPortBase<ShortDelayPort> {
public:
  void DelayUs(uint16_t us) noexcept {
    (void)us;
  }
};

int main() {
  ShortDelayPort port;
  ads7952::ADS7952<ShortDelayPort> adc(port);
  return static_cast<int>(adc.EnsureInitialized().error);
}
