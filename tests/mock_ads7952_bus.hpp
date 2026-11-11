/**
 * @file mock_ads7952_bus.hpp
 * @brief SPI mock ports over the chip model.
 *
 * `MockPortBase<Self>` implements the required hooks of the SPI port over a `MockBusState`. Derive
 * from it to build a port with any set of optional hooks:
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `MinimalPort` | the required ones plus `DelayUs()` |
 * | `FullPort` | also `Log()` |
 */
#ifndef ADS7952_MOCK_ADS7952_BUS_HPP
#define ADS7952_MOCK_ADS7952_BUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "ads7952_spi_interface.hpp"
#include "ads7952_types.hpp"
#include "mock_ads7952.hpp"

namespace ads7952_test {

/// The model's part for a driver `Variant` (the values agree: tests/test_model.cpp).
[[nodiscard]] constexpr chip::Part ModelPart(ads7952::Variant variant) noexcept {
  return static_cast<chip::Part>(static_cast<uint8_t>(variant));
}
/// The driver's `Variant` for a model part.
[[nodiscard]] constexpr ads7952::Variant DriverVariant(chip::Part part) noexcept {
  return static_cast<ads7952::Variant>(static_cast<uint8_t>(part));
}
/// The model's package for a driver `Package`.
[[nodiscard]] constexpr chip::Package ModelPackage(ads7952::Package package) noexcept {
  return static_cast<chip::Package>(static_cast<uint8_t>(package));
}
/// The driver's `Package` for a model package.
[[nodiscard]] constexpr ads7952::Package DriverPackage(chip::Package package) noexcept {
  return static_cast<ads7952::Package>(static_cast<uint8_t>(package));
}

/// Shared state of the mock ports: the chip, fault injection and counters.
struct MockBusState {
  Ads7952Model chip;
  bool bus_up_ok = true;            ///< EnsureInitialized() result.
  int fail_transfer = -1;           ///< Fail the transfer with this index (0-based); -1 never.
  bool fail_after_clocking = false; ///< The failed transfer reached the chip all the same.
  int lose_transfer = -1;           ///< Report this transfer as done, but the chip never sees it.
  int replace_transfer = -1;        ///< Replace the reply of this transfer with `replace_word`.
  uint16_t replace_word = 0;        ///< What MISO carries in `replace_transfer`.
  int transfers = 0;                ///< Transfers so far.
  int bus_up_calls = 0;             ///< EnsureInitialized() calls so far.
  int bad_transfers = 0; ///< Transfers outside the port contract (not 2 bytes, no buffer).
  std::array<uint16_t, 4096> words{}; ///< Every word sent, in order.

  bool DoTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    const int index = transfers++;
    if (tx == nullptr || rx == nullptr || len != chip::kFrameBytes) {
      bad_transfers++;
      return false;
    }
    const auto word =
        static_cast<uint16_t>((static_cast<uint32_t>(tx[0]) << 8U) | static_cast<uint32_t>(tx[1]));
    if (static_cast<std::size_t>(index) < words.size()) {
      words[static_cast<std::size_t>(index)] = word;
    }
    if (index == fail_transfer) {
      if (fail_after_clocking) {
        (void)chip.Frame(word); // the frame went out; the port reported an error afterwards
      }
      return false;
    }
    uint16_t reply = 0xFFFFU; // a lost frame: nothing drove MISO
    if (index != lose_transfer) {
      reply = chip.Frame(word);
    }
    if (index == replace_transfer) {
      reply = replace_word;
    }
    rx[0] = static_cast<uint8_t>(static_cast<uint32_t>(reply) >> 8U);
    rx[1] = static_cast<uint8_t>(static_cast<uint32_t>(reply) & 0xFFU);
    return true;
  }

  /// The word of transfer `index`.
  [[nodiscard]] uint16_t Word(int index) const noexcept {
    return words[static_cast<std::size_t>(index)];
  }

  /// How many of the transfers from `from` on sent a word whose DI15:12 is `command` (0 to 15).
  [[nodiscard]] int CountCommand(uint8_t command, int from = 0) const noexcept {
    int n = 0;
    for (int i = from; i < transfers && static_cast<std::size_t>(i) < words.size(); ++i) {
      n += (static_cast<uint32_t>(Word(i)) >> chip::kCommandShift) == command ? 1 : 0;
    }
    return n;
  }
};

/// The required SPI hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public ads7952::SpiInterface<Self> {
public:
  MockBusState state;

  bool EnsureInitialized() noexcept {
    state.bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return state.DoTransfer(tx, rx, len);
  }
};

/// Required hooks plus the delay; time passes in the model as the driver waits.
class MinimalPort : public MockPortBase<MinimalPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// Every optional hook, with counters.
class FullPort : public MockPortBase<FullPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.
  int log_lines = 0;       ///< Log() calls.
  int error_lines = 0;     ///< Log() calls at LogLevel::Error.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  void Log(ads7952::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == ads7952::LogLevel::Error) {
      error_lines++;
    }
  }
};

} // namespace ads7952_test

#endif // ADS7952_MOCK_ADS7952_BUS_HPP
