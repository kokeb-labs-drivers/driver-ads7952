/**
 * @file ads7952.hpp
 * @brief ADS7952 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `ads7952::ADS7952<Port>` drives one ADS7950, ADS7951, ADS7952 or ADS7953 12-bit SAR ADC through
 * a platform port you write (see `ads7952_spi_interface.hpp`). The driver has no platform code,
 * allocates nothing, throws nothing, takes no lock and is not thread-safe: give each object one
 * owner, or guard it with one mutex. Section, table and figure numbers refer to the datasheet,
 * TI SLAS605C (revised July 2018).
 */
#ifndef ADS7952_HPP
#define ADS7952_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ads7952_detail.hpp"
#include "ads7952_registers.hpp"
#include "ads7952_spi_interface.hpp"
#include "ads7952_types.hpp"
#include "ads7952_version.h"

namespace ads7952 {

namespace detail {

// Optional hooks: a `Declares*` concept only tests the name, the `Has*` concept the exact
// signature, `noexcept` and the result type; the driver rejects a hook that is declared but does
// not match (KB-CPP20-007).

/// The port declares something called `DelayUs`.
template <typename Port>
concept DeclaresDelayUs = requires(Port& port) { port.DelayUs(1U); };

/// The port waits: `void DelayUs(uint32_t us) noexcept`. A narrower parameter would truncate long
/// waits.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// Wait through the port. A port without `DelayUs()` fails to compile at the first call that needs
/// a wait: the power-up time after a reset is a requirement, never skipped.
template <typename Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>,
                  "ads7952: the port must implement `void DelayUs(uint32_t us) noexcept`");
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

/// The channel that follows `channel` in a sequence `mask`, wrapping round (Auto-1 and Auto-2 scan
/// in ascending order and start again at the first channel, sections 8.4.5 and 8.4.6); `channel`
/// itself for an empty mask. Rotating the mask puts channel + 1 at bit 0, so the next channel is
/// the count of trailing zeros away (KB-CPP20-028).
[[nodiscard]] constexpr uint8_t NextInSequence(uint16_t mask, uint8_t channel) noexcept {
  const uint32_t from = (static_cast<uint32_t>(channel) + 1U) & 0x0FU;
  const auto rotated = std::rotr(mask, static_cast<int>(from));
  if (rotated == 0U) {
    return channel;
  }
  const auto step = static_cast<uint32_t>(std::countr_zero(rotated));
  return static_cast<uint8_t>((from + step) & 0x0FU);
}

/// Number of channels in a mask.
[[nodiscard]] constexpr uint8_t CountChannels(uint16_t mask) noexcept {
  return static_cast<uint8_t>(std::popcount(mask));
}

/// The mask of the Auto-2 sequence: channel 0 to `last` (0 to 15; masked to 4 bits).
[[nodiscard]] constexpr uint16_t Auto2Mask(uint8_t last) noexcept {
  return static_cast<uint16_t>((uint32_t{2} << (static_cast<uint32_t>(last) & 0x0FU)) - 1U);
}

// Sections 8.4.5 and 8.4.6: ascending order with wrap-round.
static_assert((NextInSequence(0x0FFF_u16, 3_u8) == 4U) &&
              (NextInSequence(0x0FFF_u16, 11_u8) == 0U));
static_assert((NextInSequence(0x8001_u16, 15_u8) == 0U) &&
              (NextInSequence(0x8001_u16, 0_u8) == 15U));
static_assert((NextInSequence(0x0010_u16, 4_u8) == 4U) && (NextInSequence(0x0000_u16, 7_u8) == 7U));
static_assert((CountChannels(0xFFFF_u16) == 16U) && (CountChannels(0x00A5_u16) == 4U));
static_assert((Auto2Mask(0_u8) == 0x0001U) && (Auto2Mask(11_u8) == 0x0FFFU) &&
              (Auto2Mask(15_u8) == 0xFFFFU));

/// DI06:00 of every mode word the driver sends for `config`: the range, power-down off, DI04 = 0
/// (the reply carries the channel address), the GPIO output levels (Tables 1, 2, 5).
[[nodiscard]] constexpr uint16_t ControlBits(const Config& config) noexcept {
  return static_cast<uint16_t>(
      ((config.range == Range::TwoVref) ? static_cast<uint32_t>(reg::kRangeTwoVref) : 0U) |
      (static_cast<uint32_t>(config.gpio.output_levels) & reg::kGpioDataMask));
}

/// The GPIO program word for `config` (Table 11). DI11:10 stay 00. On VQFN, GPIO1 to GPIO3 are not
/// bonded out and must be programmed as outputs ("Program 1 for QFN packaged devices").
[[nodiscard]] constexpr uint16_t GpioProgramWord(const Config& config) noexcept {
  const GpioConfig& g = config.gpio;
  const uint32_t qfn = (config.package == Package::Vqfn) ? 0x000EU : 0x0000U;
  return static_cast<uint16_t>(
      static_cast<uint32_t>(reg::kGpioProgram) |
      (g.gpio3_power_down_input ? static_cast<uint32_t>(reg::kGpio3PowerDown) : 0U) |
      (g.gpio2_range_input ? static_cast<uint32_t>(reg::kGpio2Range) : 0U) |
      ((static_cast<uint32_t>(ToUnderlying(g.alarm_output)) & 0x07U) << reg::kGpioAlarmShift) |
      ((static_cast<uint32_t>(g.output_mask) | qfn) & reg::kGpioDirectionMask));
}

/// `config` with every alarm threshold changed in its upper 10 bits (the bits the chip keeps,
/// Table 13): writing from it to `config` sends every alarm register.
[[nodiscard]] constexpr Config EveryThresholdDiffers(const Config& config) noexcept {
  Config from = config;
  for (AlarmThreshold& t : from.alarms) {
    t.low_code = static_cast<uint16_t>(~static_cast<uint32_t>(t.low_code) & kMaxCode);
    t.high_code = static_cast<uint16_t>(~static_cast<uint32_t>(t.high_code) & kMaxCode);
  }
  return from;
}

// The reset configuration programs the reset values (Table 11: every pin an input, no function).
static_assert((ControlBits(Config{}) == 0U) && (GpioProgramWord(Config{}) == reg::kGpioProgram));
static_assert(GpioProgramWord(Config{.package = Package::Vqfn}) == 0x400EU);

/// The two channels the identity check selects: their addresses differ in every bit the variant
/// uses, so neither a stuck MISO line nor a fixed reply pattern can echo both.
struct IdentityChannels {
  uint8_t first;
  uint8_t second;
};

[[nodiscard]] constexpr IdentityChannels IdentityFor(Variant variant) noexcept {
  switch (variant) {
  case Variant::Ads7950:
    return {.first = 2, .second = 1}; // 10, 01
  case Variant::Ads7951:
    return {.first = 5, .second = 2}; // 101, 010
  case Variant::Ads7952:
  case Variant::Ads7953:
  default:
    break;
  }
  return {.first = 10, .second = 5}; // 1010, 0101
}

/// True when the two identity channels exist on `variant` and differ in every address bit it uses.
[[nodiscard]] constexpr bool IdentityCovers(Variant variant) noexcept {
  const IdentityChannels ids = IdentityFor(variant);
  const auto channels = static_cast<uint32_t>(ChannelCount(variant));
  const uint32_t used = (channels > 8U) ? 0x0FU : (channels - 1U);
  return (ids.first < channels) && (ids.second < channels) &&
         (((static_cast<uint32_t>(ids.first) ^ static_cast<uint32_t>(ids.second)) & used) == used);
}

static_assert(IdentityCovers(Variant::Ads7950) && IdentityCovers(Variant::Ads7951) &&
              IdentityCovers(Variant::Ads7952) && IdentityCovers(Variant::Ads7953));

} // namespace detail

/**
 * @defgroup ads7952_driver ADS7952 driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the frame-cost table below must keep one row per line.
/**
 * @brief Driver for one ADS7950, ADS7951, ADS7952 or ADS7953 on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()`, the one way
 * to start (bus up, reset, configuration written, identity check), use, optionally
 * `Deinitialize()`. Every call that touches the chip returns `Error::NotInitialized` until
 * `EnsureInitialized()` has succeeded; no call starts the chip as a side effect.
 *
 * **The frame pipeline.** A channel chosen in frame N is acquired in frame N + 1 and its result
 * comes back in frame N + 2 (Figures 51, 53, 56); a range written in frame N applies from the
 * result of frame N + 2 as well. The driver keeps track of what the chip is doing (mode, channel,
 * replies still in flight), so a call sends only the frames it needs, and it checks the channel
 * address of every reply it returns: a result is never attributed to the wrong channel.
 *
 * **A reset behind the driver's back** (a brown-out) puts every register back to its reset value
 * (section 8.4.3, Table 11, section 8.5.3) and makes the next conversion invalid (section 7.6); the
 * chip has no flag for it. The driver checks the address of every reply it can predict, the ones
 * a new select discards included, and a reply from channel 0 (the power-on channel) where it
 * expects another is `DeviceReset`. After it, and after any `ChannelMismatch` or
 * `SequenceMismatch`, the driver is uninitialised (fail-safe): `EnsureInitialized()` resets and
 * programs the chip in the datasheet order and discards the invalid first conversion. A reset
 * at any moment the chip rests on channel 0 in Manual mode (after a read of channel 0, a list
 * ending with it, in any mix of reads) shows in no reply, and the results right after a reset
 * (the invalid conversion, one in Range 1) can be returned before it is seen. Mitigations: the
 * opt-in `Config::park_channel`, and `ApplyConfig()`, which writes every register whatever the
 * driver's copy says (docs/limitations.md).
 *
 * | Call | Frames |
 * | --- | --- |
 * | `ReadChannel(ch)` | 1 when `ch` is the selected Manual channel, 2 right after `SetRange()`, 3 otherwise; with `Config::park_channel`, `ReadChannel(0)` one more (4 when repeated) |
 * | `ReadChannels(list)` | list length + 2 |
 * | `ReadAuto1()`, `ReadAuto2()` | k (the channels of the sequence) while the sequence runs, k + 1 right after `SetRange()` or a new sequence, k + 2 when it has to be entered |
 * | `SetRange()` | 1 |
 * | `SetAuto1Channels()` | 2 (`Verify::ReadBack`: and one scan) |
 * | `SetAuto2LastChannel()` | 1 (`Verify::ReadBack`: and one scan) |
 * | `SetAlarmThresholds()` | 4 |
 * | `SetGpioOutputs()` | 2 (`Verify::ReadBack`: 3) |
 * | `ReadGpio()` | 2 |
 * | `EnsureInitialized()` | 13, plus 3 to 10 per alarm group that is not at its reset value, plus 3 with a `Config::monitor` |
 * | `ApplyConfig()` (running) | 6 + 10 per alarm group: 16 (ADS7950), 26, 36 (ADS7952), 46 (ADS7953) |
 * | `CheckScale()` | as `ReadChannel()` of the monitor channel: 1 to 3 |
 * | `CheckScaleCode()`, `ToMicrovolts()` | 0 |
 * | `Deinitialize()` | 2 |
 *
 * Optional port hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 *
 * @tparam Port Your class derived from `SpiInterface<Port>`.
 */
// clang-format on
template <SpiPort Port>
class ADS7952 {
  static_assert(
      !detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
      "ads7952: the DelayUs hook must be `void DelayUs(uint32_t us) noexcept`, so that no "
      "wait is truncated");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "ads7952: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");

public:
  /**
   * @brief Bind the driver to a port and store the configuration. No I/O, cannot fail.
   * @param port The port; it must outlive the driver.
   * @param config Checked by `EnsureInitialized()`, before any I/O.
   */
  explicit ADS7952(Port& port, const Config& config = {}) noexcept : bus_(port), config_(config) {}

  /** @brief One driver object per chip. */
  ADS7952(const ADS7952&) = delete;
  /** @brief One driver object per chip. */
  ADS7952& operator=(const ADS7952&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Reset the chip, write the configuration and check that an ADS795x answers, once: the
   * only way to initialise the driver.
   *
   * In order: `Validate()` the configuration (no I/O), bring the bus up, end any program sequence
   * an earlier failure left open (two frames), reset every register (the GPIO program frame with
   * DI09 = 1, then the frame in which the chip resets itself, then `kPowerUpUs`), write the GPIO,
   * Auto-1 and Auto-2 program registers and every alarm group that differs from its reset value,
   * then the identity check: in Manual mode, select a channel, keep it one frame, select a second
   * one whose address differs in every bit (10 and 5 on the ADS7952), and demand that the replies
   * two frames after each carry exactly those addresses. A MISO line stuck low or high, a fixed
   * reply pattern, a loop-back and an SDI line that does not reach the chip (whatever mode the chip
   * was left in) all fail it. With a `Config::monitor`, last, the scale check: one conversion of
   * the monitor channel must fall in its window (`CheckScaleCode()`), which proves the range and
   * the reference before any result is returned. The chip is left in Manual mode. After it
   * succeeds, later calls return `Ok` at once with no I/O; after it fails, the next call starts
   * again from the beginning.
   *
   * @return `Ok`, `InvalidArgument` or `Unsupported` (the configuration fails `Validate()`; no
   *         I/O), `Bus`, `NoDevice` (every identity reply 0xFFFF), `DeviceIdMismatch`, or
   *         `RangeMismatch` or `ScaleMismatch` (the scale check).
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()` and before `Deinitialize()`. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Power the chip down and mark the driver uninitialised: one mode word with DI05 = 1, then
   * the frame at whose end the chip powers down (section 8.3.2).
   *
   * The driver is uninitialised afterwards even if a frame failed; the next `EnsureInitialized()`
   * resets and checks the chip again, which is also how a handler recovers after a fault. On an
   * uninitialised driver it does nothing and returns `Ok`.
   *
   * @return `Ok` or `Bus`.
   */
  Result<void> Deinitialize() noexcept;

  /**
   * @brief Replace the configuration.
   *
   * Before the start it only stores it, with no I/O. On a running driver it writes every
   * register, changed or not, in the order of section 8.4.3: the GPIO program register, the Auto-1
   * and Auto-2 program registers, every alarm threshold of every group (Table 13), then a mode word
   * with the range and the GPIO levels and the frame on which the outputs change. The driver's copy
   * is never trusted, because no register can be read back and a reset while the chip converts
   * channel 0 shows in no reply: `ApplyConfig(GetConfig())` puts the whole configuration back.
   * 6 + 10 frames per alarm group (36 on the ADS7952). `vref_mv` and `va_min_mv` are never sent.
   *
   * @param config Checked with `Validate()`.
   * @return `Ok`, `InvalidArgument` (it fails `Validate()`, or it changes the variant or package
   *         of a running driver; nothing sent), `Unsupported`, or `Bus`. After `Bus` the stored
   *         configuration is unchanged and the chip may hold part of the new one: restart with
   *         `Deinitialize()` and `EnsureInitialized()`.
   */
  Result<void> ApplyConfig(const Config& config) noexcept;

  /**
   * @brief Whether the chip may hold something other than `GetConfig()`. No I/O.
   *
   * True after a setter or `ApplyConfig()` failed with `Bus`, or a `Verify::ReadBack` did not
   * confirm a write (the frame may have reached the chip and taken effect: a range, GPIO levels, a
   * sequence, alarm thresholds), until every register is written again: `ApplyConfig(GetConfig())`
   * on the running driver, or `Deinitialize()` then `EnsureInitialized()` (on a running driver
   * `EnsureInitialized()` alone does nothing and leaves it set). `GetConfig()` then shows the
   * values last written successfully, never the ones of the failed write.
   * @return true while the driver's copy is not known to be the chip's state.
   */
  [[nodiscard]] bool IsConfigUncertain() const noexcept {
    return config_uncertain_;
  }

  /** @return The configuration last applied or stored. No I/O. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /** @return The number of analog inputs of the configured variant. No I/O. */
  [[nodiscard]] uint8_t GetChannelCount() const noexcept {
    return ChannelCount(config_.variant);
  }

  /// @}
  /// @name Conversions
  /// @{

  /**
   * @brief One conversion of `channel` in Manual mode.
   *
   * When the chip already converts `channel` in Manual mode (after a `ReadChannel()` of the same
   * channel, or a `ReadChannels()` that ended with it), one frame; right after `SetRange()`, two
   * (the first reply was converted in the old range); otherwise three: the Manual word that
   * selects `channel` (with the range and the GPIO levels, DI11 = 1), the frame that acquires it,
   * and the one that returns it. The frame that returns the result is the Manual word for
   * `channel` again, so every read sends the channel and the range a reset would have changed
   * (section 8.4.3). The reply's address must be `channel`; after a select from a settled channel
   * the two replies discarded must carry that channel.
   *
   * @param channel 0 to `GetChannelCount()` - 1.
   * @return The 12-bit code, or `NotInitialized`, `InvalidArgument` (nothing sent), `Bus`,
   *         `DeviceReset` (a reply came from channel 0, the power-on channel) or
   *         `ChannelMismatch` (another address). After either of the last two the driver is
   *         uninitialised: call `EnsureInitialized()`.
   */
  Result<uint16_t> ReadChannel(uint8_t channel) noexcept;

  /**
   * @brief One conversion of each listed channel, pipelined in Manual mode (Figure 51): a Manual
   * word per channel, then two frames; the result of each channel arrives two frames after its
   * select. `channels.size()` + 2 frames. Every reply's address must be the channel it stands for.
   *
   * @param channels 1 to 16 channels, each below `GetChannelCount()`; repeats allowed.
   * @param codes Where the codes go, in the order of `channels`; at least as long.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent, `codes` untouched), `Bus`,
   *         `DeviceReset` or `ChannelMismatch` (the driver is then uninitialised; the two first
   *         replies, from before the first select, must carry a settled Manual channel). After a
   *         failure past the argument checks the codes are 0.
   */
  Result<void> ReadChannels(std::span<const uint8_t> channels, std::span<uint16_t> codes) noexcept;

  /**
   * @brief One pass of the Auto-1 sequence (`Config::auto1_channels`): the hot path.
   *
   * While the chip runs the sequence (after a previous `ReadAuto1()`), k frames for k channels,
   * each the Auto-1 word with DI11 = 1 and DI10 = 0 (the range and GPIO levels again, no restart):
   * the chip steps through the sequence by itself and every reply is a result. Unlike a
   * "continue" word, it puts the range back and, after a reset, makes the chip enter Auto-1 with
   * the reset sequence, so even a sequence of channel 0 alone shows the reset within a few passes.
   * Right after `SetRange()` or `SetAuto1Channels()`, k + 1; otherwise the Auto-1 word that enters
   * the mode and restarts the sequence (with the range and the GPIO levels) and one frame in flight
   * come first, k + 2. The k replies must be the k channels of the sequence, each once, in the
   * chip's ascending, wrapping order; the pass may start at any channel.
   *
   * @return The bank, or `NotInitialized`, `Bus`, `DeviceReset` (a reply from channel 0 out of
   *         turn: the chip is back in Manual mode on channel 0) or `SequenceMismatch` (the chip
   *         left Auto-1 or lost its mask). After either of the last two the driver is
   *         uninitialised: call `EnsureInitialized()`.
   */
  Result<Bank> ReadAuto1() noexcept;

  /**
   * @brief One pass of the Auto-2 sequence, channel 0 to `Config::auto2_last_channel`: as
   * `ReadAuto1()` with the Auto-2 word.
   * @return The bank, or `NotInitialized`, `Bus`, `DeviceReset` or `SequenceMismatch` (the
   *         driver is then uninitialised).
   */
  Result<Bank> ReadAuto2() noexcept;

  /**
   * @brief Bring-up trace: select `channel` in Manual mode (DI11 = 1) and return every reply word
   * unparsed, the select frame's included; the other frames are "continue" words:
   * `replies.size()` frames. The address nibble of `replies[2]` onwards should be `channel`; a
   * nibble that never follows the select means SDI does not reach the chip. A single shot with no
   * retry and no check of the replies (handbook KB-CPP-129); its one side effect is that the chip
   * is left in Manual mode on `channel`, as after `ReadChannel(channel)`.
   *
   * @param channel 0 to `GetChannelCount()` - 1.
   * @param replies 1 to `kMaxTraceFrames` words.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent) or `Bus` (the words are then
   *         0).
   */
  Result<void> TraceFrames(uint8_t channel, std::span<uint16_t> replies) noexcept;

  /**
   * @brief The input voltage a code stands for in the configured range and reference. No I/O.
   * @param code 0 to 4095.
   * @return Microvolts at the bottom of the code's step (one LSB is full scale / 4096).
   */
  [[nodiscard]] uint32_t ToMicrovolts(uint16_t code) const noexcept {
    return CodeToMicrovolts(code, config_.range, config_.vref_mv);
  }

  /**
   * @brief The input voltage of one channel of a scan, in the range that scan was converted in
   * (`Bank::range`), not the range configured now. No I/O.
   * @param bank A result of `ReadAuto1()` or `ReadAuto2()`.
   * @param channel 0 to 15; a channel the bank does not hold gives 0.
   * @return Microvolts at the bottom of the code's step.
   */
  [[nodiscard]] uint32_t ToMicrovolts(const Bank& bank, uint8_t channel) const noexcept {
    return bank.Has(channel) ? CodeToMicrovolts(bank.code[channel], bank.range, config_.vref_mv)
                             : 0U;
  }

  /**
   * @brief Prove the scale now: one conversion of the `Config::monitor` channel, judged by
   * `CheckScaleCode()`. An explicit, opt-in call: nothing on the conversion path runs it.
   *
   * Costs what `ReadChannel()` of that channel costs (1 to 3 frames) and leaves the chip in Manual
   * mode on it, so the next `ReadAuto1()` enters Auto-1 again (two frames). Its select carries the
   * range, so it proves that the chip takes the range the driver sends and that the reference is
   * there; a range lost during a long Auto-1 run shows in the scan itself: check the monitor's
   * code of each `Bank` with `CheckScaleCode()`, which costs no frame.
   *
   * @return The monitor's code; or `NotInitialized`, `InvalidArgument` (no monitor; nothing sent),
   *         `Bus`, `ChannelMismatch`, `RangeMismatch`, `ScaleMismatch` or `DeviceReset` (a reply
   *         from channel 0 out of turn, or Range 1 where the driver sends Range 2: the reset value
   *         of DI06). After any of these but `Bus` the driver is uninitialised (fail-safe).
   */
  Result<uint16_t> CheckScale() noexcept;

  /**
   * @brief Judge a code of the monitor channel against `Config::monitor`, with no I/O: use it on
   * every `Bank` or DMA-decoded word that holds the monitor channel.
   * @param code The monitor channel's code, converted in `Config::range`.
   * @return `Ok`, `RangeMismatch`, `ScaleMismatch`, or `InvalidArgument` (no monitor).
   */
  [[nodiscard]] Error CheckScaleCode(uint16_t code) const noexcept {
    return ads7952::CheckScaleCode(config_, code);
  }

  /**
   * @brief The last reply word the driver received, for logs: 0xFFFF before the first. No I/O.
   * @return After a failed `EnsureInitialized()`, 0xFFFF means MISO floats high, 0x0000 MISO is
   *         stuck low; an address nibble of 0 with any code means the chip did not get the select.
   */
  [[nodiscard]] uint16_t GetLastReply() const noexcept {
    return last_reply_;
  }

  /// @}
  /// @name Settings
  /// @{

  /**
   * @brief Change the input range: one mode word for the current mode with the new DI06 (Table 1,
   * 2, 5). Results clocked out from the second frame after it use the new range; the conversion
   * calls skip the one in between by themselves.
   * @param range The new range.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (not an enumerator, Range 2 with 2 x VREF
   *         above `va_min_mv`, or a `Config::monitor` that does not fit the new range; nothing
   * sent), `Unsupported` (GPIO2 is the range input; nothing sent) or `Bus`.
   */
  Result<void> SetRange(Range range) noexcept;

  /**
   * @brief Program the Auto-1 sequence: two frames (Table 3). In Auto-1 the chip goes on with the
   * next enabled channel of the new mask.
   *
   * With `Verify::ReadBack`, a scan follows (as `ReadAuto1()`, k + 1 or k + 2 frames) whose
   * addresses must be the new sequence; it leaves the sequence running, so the next `ReadAuto1()`
   * costs k frames.
   *
   * @param mask Bit n for channel n; at least one channel, none above the variant's.
   * @param verify `Verify::None` (the default) or `Verify::ReadBack`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Bus` or `WriteMismatch`
   *         (the driver is then uninitialised: a lost frame and a reset look alike).
   */
  Result<void> SetAuto1Channels(uint16_t mask, Verify verify = Verify::None) noexcept;

  /**
   * @brief Program the last channel of the Auto-2 sequence: one frame (Table 6). In Auto-2 the
   * chip starts again at channel 0. `Verify::ReadBack` adds one scan, as for `SetAuto1Channels()`.
   * @param last 0 to `GetChannelCount()` - 1.
   * @param verify `Verify::None` (the default) or `Verify::ReadBack`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Bus` or `WriteMismatch`
   *         (the driver is then uninitialised).
   */
  Result<void> SetAuto2LastChannel(uint8_t last, Verify verify = Verify::None) noexcept;

  /**
   * @brief Program both alarm thresholds of a channel: the group frame, the low threshold, the
   * high threshold with the exit bit, and one frame to leave the sequence (Table 13). 4 frames.
   * The chip keeps the upper 10 bits of each code. There is no way to read them back.
   * @param channel 0 to `GetChannelCount()` - 1.
   * @param thresholds The codes, each 0 to 4095.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent) or `Bus`.
   */
  Result<void> SetAlarmThresholds(uint8_t channel, AlarmThreshold thresholds) noexcept;

  /**
   * @brief Drive the GPIO outputs: one mode word with the new DI03:00, then the frame on whose CS
   * falling edge the chip drives them (section 8.5.2). 2 frames; the pins show the levels when it
   * returns.
   *
   * With `Verify::ReadBack` the second frame is a mode word that asks for the GPIO levels (DI04)
   * and a third returns them in DO15:12, after the pins changed; they must equal `levels` on every
   * output pin (`WriteMismatch`). 3 frames. It catches a lost frame and a pin held by the board.
   *
   * @param levels Bit n for GPIOn; only pins in `Config::gpio.output_mask`.
   * @param verify `Verify::None` (the default) or `Verify::ReadBack`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Bus` or `WriteMismatch`.
   *         Once the first frame went out the chip holds the levels and `GetConfig()` shows them,
   *         whatever comes after.
   */
  Result<void> SetGpioOutputs(uint8_t levels, Verify verify = Verify::None) noexcept;

  /**
   * @brief Read the levels of GPIO3:0, inputs and outputs: a mode word with DI04 = 1, then one
   * with DI04 = 0 whose reply carries them in DO15:12 (Tables 1, 2, 5). 2 frames.
   * @return Bit n for GPIOn (GPIO0 only on VQFN), or `NotInitialized` or `Bus`.
   */
  Result<uint8_t> ReadGpio() noexcept;

  /// @}

private:
  /// What the chip converts, as far as the driver knows.
  enum class Mode : uint8_t { Unknown, Manual, Auto1, Auto2 };

  Result<void> initialize() noexcept;
  Result<void> programAll() noexcept;
  Result<void> resetChip() noexcept;
  Result<void> checkIdentity() noexcept;
  Result<uint16_t> checkScale() noexcept;
  Result<void> writeConfig(const Config& to) noexcept;
  Result<void> writeGpioProgram(const Config& to) noexcept;
  Result<void> writeAlarms(const Config& from, const Config& to) noexcept;
  Result<void> writeAlarmGroup(const Config& from, const Config& to, uint8_t group) noexcept;
  Result<void> writeControl(const Config& to) noexcept;
  Result<void> programAuto1(uint16_t mask) noexcept;
  Result<void> programAuto2(uint8_t last) noexcept;
  Result<uint16_t> readSelected(uint8_t channel, uint8_t rest) noexcept;
  Result<void> park(uint8_t channel, uint8_t rest) noexcept;
  Result<void> readPipelined(std::span<const uint8_t> channels, std::span<uint16_t> codes) noexcept;
  Result<Bank> scan(Mode mode, uint16_t mask) noexcept;
  Result<Bank> collect(uint16_t mask) noexcept;
  Result<void> discardStale() noexcept;
  Result<uint16_t> modeFrame(uint16_t control_bits) noexcept;
  Result<uint16_t> select(uint8_t channel) noexcept;
  Result<uint16_t> selectChecked(uint8_t channel) noexcept;
  [[nodiscard]] uint8_t settledChannel() const noexcept;
  Error lostTrack(Error mismatch, uint8_t address) noexcept;
  Result<uint16_t> frame(uint16_t word) noexcept;
  /// No channel: no reply address to predict, or a mismatch that is not a reset's signature.
  static constexpr uint8_t kNotAReset = 0xFFU;
  [[nodiscard]] bool validChannel(uint8_t channel) const noexcept {
    return channel < ChannelCount(config_.variant);
  }
  [[nodiscard]] bool validList(std::span<const uint8_t> channels,
                               std::span<const uint16_t> codes) const noexcept {
    if (channels.empty() || (channels.size() > kMaxChannels) || (codes.size() < channels.size())) {
      return false;
    }
    return std::all_of(channels.begin(), channels.end(),
                       [this](uint8_t channel) { return validChannel(channel); });
  }
  [[nodiscard]] static constexpr bool isVerify(Verify verify) noexcept {
    return (verify == Verify::None) || (verify == Verify::ReadBack);
  }
  template <typename T>
  Result<T> fail(Error error, const char* message) noexcept;
  Result<void> failedWrite(Error error, const char* message) noexcept;

  detail::FrameBus<Port> bus_;
  Config config_{};
  Mode mode_{Mode::Unknown};
  uint8_t manual_channel_{0};
  uint8_t stale_{0}; ///< Replies still on their way that belong to the previous selection.
  uint16_t last_reply_{0xFFFF};
  bool initialized_{false};
  bool config_uncertain_{false}; ///< A write failed: the chip may hold part of it.
};

/** @} */

} // namespace ads7952

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/ads7952.ipp" // IWYU pragma: export

#endif // ADS7952_HPP
