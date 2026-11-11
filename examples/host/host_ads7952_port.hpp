/**
 * @file host_ads7952_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated ADS7952, so the example runs on any
 * PC.
 *
 * `ads7952_test::MockPortBase` (tests/mock_ads7952_bus.hpp) supplies the required bus hooks over
 * the chip model the host tests use. This class adds what a real port adds: `DelayUs()` (here
 * it advances the simulated chip's clock instead of waiting) and `Log()` to the console. A port for
 * a real bus on a PC (Linux spidev, a USB bridge) implements the same hooks: see docs/porting.md.
 */
#ifndef ADS7952_HOST_ADS7952_PORT_HPP
#define ADS7952_HOST_ADS7952_PORT_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "mock_ads7952.hpp"

/// The host port: simulated chip, real console.
class HostAds7952Port : public ads7952_test::MockPortBase<HostAds7952Port> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 1'000'000;
  static constexpr uint8_t kSpiMode = 0;
  static constexpr std::size_t kTransferBytes = 2;
  static_assert(kClockHz <= ads7952::kMaxClockHz, "clock above the ADS795x's f(SCLK) max");
  static_assert(kSpiMode == ads7952::kSpiMode, "the ADS795x latches SDI in SPI mode 0");
  static_assert(kTransferBytes == ads7952::kFrameBytes, "one transfer is one 16-bit frame");

  /// Time passes in the simulated chip; nothing sleeps.
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }

  /// Driver messages (they carry their own `ads7952:` prefix) go to the console, one line each.
  void Log(ads7952::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // ADS7952_HOST_ADS7952_PORT_HPP
