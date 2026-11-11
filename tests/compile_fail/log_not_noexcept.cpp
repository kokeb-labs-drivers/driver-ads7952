/**
 * @file log_not_noexcept.cpp
 * @brief Must NOT compile: an optional Log hook that is declared but not `noexcept` would be
 * skipped silently, so the driver rejects it (handbook KB-CPP20-007).
 *
 * EXPECT: Log hook must be
 */
#include <cstdint>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"

class ThrowingLogPort : public ads7952_test::MockPortBase<ThrowingLogPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }
  void Log(ads7952::LogLevel level, const char* message) {
    (void)level;
    (void)message;
  }
};

int main() {
  ThrowingLogPort port;
  ads7952::ADS7952<ThrowingLogPort> adc(port);
  return static_cast<int>(adc.EnsureInitialized().error);
}
