/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <array>
#include <cstddef>
#include <cstdint>

#include "ads7952.hpp"

bool platform_spi_init() noexcept;
bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept;
void platform_delay_us(uint32_t us) noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public ads7952::SpiInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_spi_init();
  }
  // One 16-bit frame per call: CS low for both bytes, high after (the chip converts on each CS
  // falling edge).
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }

  // Required: the power-up time after the reset in EnsureInitialized().
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(ads7952::LogLevel level, const char* message) noexcept {
    if (level > ads7952::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }
};
// --8<-- [end:full-port]

// --8<-- [start:trace]
// Bring-up: is SDI reaching the chip? The address nibble of words[2] onwards must be 11.
bool SdiReachesChip(ads7952::ADS7952<FullPort>& adc) {
  std::array<uint16_t, 6> words{};
  if (!adc.TraceFrames(11, words)) {
    return false;
  }
  return ads7952::reg::ReplyAddress(words[5]) == 11;
}
// --8<-- [end:trace]
