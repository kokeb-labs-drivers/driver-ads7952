// clang-format off: the hook table below must keep one row per line.
/**
 * @file ads7952_spi_interface.hpp
 * @brief The SPI port of the ADS7952 driver: the CRTP base, the `SpiPort` concept and the 16-bit
 * frame.
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `ads7952::SpiInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards |
 * | `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | One full-duplex transfer of `len` = 2 bytes with CS low throughout and high after |
 * | `void DelayUs(uint32_t us) noexcept` | Wait at least `us` microseconds (once, at start-up: the power-up time) |
 *
 * Every frame is one 16-bit word, MSB first, and the chip starts a conversion on each CS falling
 * edge, so CS must go high between two frames and stay low for exactly 16 SCLK cycles: the driver
 * calls `Transfer()` once per frame, always with 2 bytes and both buffers. What the chip needs from
 * the bus is below as `constexpr` values (`kSpiMode` to `kContinuousWord`, datasheet sections 7.3,
 * 7.9 and 8.1); a port checks its settings against them with `static_assert`. The SCLK rate and the
 * CS high time set how long the next channel acquires (from the 14th rising edge to the next CS
 * falling edge, section 8.1), so choose the rate for your input network. The port moves raw bytes:
 * it never retries, never interprets the frame and never configures the chip. The optional `Log()`
 * hook is described in `ads7952.hpp`.
 */
// clang-format on
#ifndef ADS7952_SPI_INTERFACE_HPP
#define ADS7952_SPI_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "ads7952_detail.hpp"
#include "ads7952_registers.hpp"
#include "ads7952_types.hpp"

namespace ads7952 {

/**
 * @name Bus requirements
 * What the ADS795x needs from the SPI bus (datasheet SLAS605C, sections 7.3, 7.9 and 8.1; the
 * values hold for every +VBD from 1.7 V to 5.25 V). A port and a board configuration check their
 * settings against these with `static_assert` (handbook KB-CPP-123).
 * @{
 */
/// SPI mode 0: CPOL 0, CPHA 0. SDI is latched on the rising SCLK edge and SDO changes on the
/// falling edge (section 8.1, Figure 2).
inline constexpr uint8_t kSpiMode = 0;
/// Each word is clocked most significant bit first (DI15 and DO15 first, Figure 2).
inline constexpr bool kMsbFirst = true;
/// Highest SCLK frequency, f(SCLK) max (sections 7.3 and 7.9).
inline constexpr uint32_t kMaxClockHz = 20'000'000;
/// Shortest time CS stays high between two frames, t_w1 (section 7.9).
inline constexpr uint32_t kMinCsHighNs = 20;
/// CS low to the first rising SCLK edge, t_su1; the largest minimum, at +VBD = 1.8 V (section 7.9).
inline constexpr uint32_t kCsSetupNs = 8;
/// Last SCLK edge to CS high: section 7.9 gives no minimum; CS may rise once the 16th SCLK falling
/// edge has passed (section 8.1).
inline constexpr uint32_t kCsHoldNs = 0;
/// Quiet time from SDO going to 3-state (at most 26 ns after the 16th SCLK falling edge, t_d3) to
/// the next CS falling edge, t_q (section 7.9). With CS high for `kMinCsHighNs` the 16th falling
/// edge must come at least `kSdoReleaseNs` + `kQuietNs` before the next CS falling edge.
inline constexpr uint32_t kQuietNs = 40;
/// Longest delay from the 16th SCLK falling edge to SDO in 3-state, t_d3 at +VBD = 1.8 V (section
/// 7.9).
inline constexpr uint32_t kSdoReleaseNs = 26;
/// `true`: the 16 clocks of a frame come back to back, with no idle SCLK between the two bytes. The
/// chip converts with SCLK (t_conv = 16 SCLK, section 7.9; section 8.1) and the datasheet specifies
/// no conversion with the clock stopped inside a frame.
inline constexpr bool kContinuousWord = true;
/// SCLK cycles per frame: one 16-bit word with CS low throughout (section 8.1).
inline constexpr uint32_t kFrameBits = 16;
/** @} */

static_assert(kFrameBits == (8U * kFrameBytes), "one frame is one 16-bit word");

/**
 * @brief CRTP base of the platform SPI port.
 *
 * Derive `class MyPort : public ads7952::SpiInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `SpiPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class SpiInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface(const SpiInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface& operator=(const SpiInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  SpiInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~SpiInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `SpiInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length (`uint16_t len`) or
 * without `noexcept` does not satisfy the concept, instead of truncating every transfer silently
 * (KB-CPP20-006). `DelayUs()` is checked by the driver class.
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept SpiPort = std::derived_from<Port, SpiInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(const uint8_t*, uint8_t*, std::size_t) noexcept>::Take(
      &Port::Transfer);
};

namespace detail {

/// One 16-bit frame: the command word out on SDI, the reply word in from SDO (Figure 2).
/// The two transfer buffers belong to the driver object, so they live wherever the application
/// places the driver (memory the bus and its DMA can reach), never on a caller's stack.
template <SpiPort Port>
class FrameBus {
public:
  explicit FrameBus(Port& port) noexcept : port_(port) {}

  Port& GetPort() & noexcept {
    return port_;
  }

  /// Send `word`, MSB first; the reply lands in `reply`. `Error::Bus` on a failed transfer, and
  /// `reply` is then 0xFFFF (what an undriven MISO would read).
  [[nodiscard]] Error Frame(uint16_t word, uint16_t& reply) noexcept {
    const uint32_t w = word;
    tx_ = {static_cast<uint8_t>(w >> 8U), static_cast<uint8_t>(w & 0xFFU)};
    rx_ = {0xFF, 0xFF};
    if (!port_.Transfer(tx_.data(), rx_.data(), tx_.size())) {
      reply = 0xFFFFU;
      return Error::Bus;
    }
    reply = static_cast<uint16_t>((static_cast<uint32_t>(rx_[0]) << 8U) |
                                  static_cast<uint32_t>(rx_[1]));
    return Error::Ok;
  }

private:
  Port& port_;
  std::array<uint8_t, kFrameBytes> tx_{}; ///< The command word, MSB first.
  std::array<uint8_t, kFrameBytes> rx_{}; ///< The reply word, MSB first.
};

} // namespace detail

} // namespace ads7952

#endif // ADS7952_SPI_INTERFACE_HPP
