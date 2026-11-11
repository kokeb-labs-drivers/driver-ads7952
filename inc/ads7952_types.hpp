/**
 * @file ads7952_types.hpp
 * @brief Public types of the ADS7952 driver: errors, `Result<T>`, variants, ranges, GPIO and alarm
 * settings, the configuration and the result of a scan.
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests and tools can
 * include it without the driver template. Section, table and figure numbers refer to the ADS79xx
 * datasheet, TI SLAS605C (revised July 2018).
 */
#ifndef ADS7952_TYPES_HPP
#define ADS7952_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "ads7952_detail.hpp"
#include "ads7952_registers.hpp"
#include "ads7952_version.h"

/** @brief The ADS7952 driver: everything it declares lives here; internals in `detail`. */
namespace ads7952 {

/**
 * @defgroup ads7952_types Driver types
 * @brief Errors, results, variants and configuration.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. The type is
 * `[[nodiscard]]`: a call whose `Error` is dropped does not compile quietly (KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran since.
  InvalidArgument = 2,  ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,              ///< The port's `Transfer()` (or `EnsureInitialized()`) returned `false`.
  Timeout = 4,          ///< Not returned by this driver: no call waits for the chip.
  DeviceIdMismatch = 5, ///< At start-up, the replies do not carry the channel addresses the chip
                        ///< must send back (MISO stuck low or carrying a pattern, SDI not reaching
                        ///< the chip, another part on the chip select).
  Crc = 6,              ///< Not returned by this driver (the chip sends no CRC).
  Busy = 7,             ///< Not returned by this driver (the chip is never busy to a frame).
  DeviceFault = 8,      ///< Not returned by this driver (the chip reports no faults).
  Unsupported = 9,      ///< A setting the configured part or package lacks (GPIO1 to GPIO3 on the
                        ///< VQFN package), or `SetRange()` while GPIO2 is the range input.
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  NoDevice = 16,         ///< At start-up, every reply was 0xFFFF: nothing drives MISO (no chip on
                         ///< the chip select, MISO pulled or floating high).
  ChannelMismatch = 17,  ///< A reply that must carry a channel's result carries another address:
                         ///< a frame was lost, the chip was reset, or the bus is broken. The
                         ///< result was not returned.
  SequenceMismatch = 18, ///< The replies of an Auto-1 or Auto-2 scan are not one pass of the
                         ///< programmed sequence: the chip left the mode, lost its program register
                         ///< (a reset or brown-out), or a frame was lost.
  WriteMismatch = 19,    ///< `Verify::ReadBack` found that the chip did not take the setting: the
                         ///< GPIO levels read back differ, or the sequence scanned is not the one
                         ///< just programmed.
  RangeMismatch = 20,    ///< The scale monitor (`Config::monitor`) reads what the other range
                         ///< would give: the chip is not converting in `Config::range`. GPIO2 is
                         ///< the range input and the board drives it to the other level, or the
                         ///< chip was reset (back to Range 1) and the driver has not seen it.
  ScaleMismatch = 21,    ///< The scale monitor reads outside its window in both ranges: the
                         ///< reference is missing, wrong or not yet settled, or the monitor input
                         ///< is not the voltage `Config::monitor` declares.
  DeviceReset = 22,      ///< The chip was reset behind the driver (a brown-out): a reply came from
                         ///< channel 0, the power-on channel (section 8.4.3), where the driver
                         ///< expected another, or the scale monitor found Range 1, the reset value.
                         ///< The registers hold their reset values (no alarm function, no GPIO
                         ///< outputs, the reset alarm thresholds); the driver is uninitialised and
                         ///< `EnsureInitialized()` programs the chip again.
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Bus"`; `"Unknown"` for a value that is not an enumerator.
 */
[[nodiscard]] constexpr const char* ToString(Error error) noexcept {
  // One case per enumerator, so -Wswitch-enum names a new one that has no name here (KB-CPP-053);
  // `default` keeps the fallback below for a value that is not an enumerator (Rule 9.4.2).
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::NoDevice:
    return "NoDevice";
  case Error::ChannelMismatch:
    return "ChannelMismatch";
  case Error::SequenceMismatch:
    return "SequenceMismatch";
  case Error::WriteMismatch:
    return "WriteMismatch";
  case Error::RangeMismatch:
    return "RangeMismatch";
  case Error::ScaleMismatch:
    return "ScaleMismatch";
  case Error::DeviceReset:
    return "DeviceReset";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised.
  Debug,     ///< Detail for bring-up.
};

/**
 * @brief Which 12-bit member of the family is fitted. The chip cannot tell, so it is configured.
 *
 * The four parts share the frame format, the modes and the registers; they differ in the number of
 * channels, which sets the reset value of the Auto-1 mask and the Auto-2 last channel and the
 * number of alarm groups (section 8.4.1, Tables 4 and 12). The 10-bit (ADS7954 to ADS7957) and
 * 8-bit (ADS7958 to ADS7961) parts are not supported.
 */
enum class Variant : uint8_t {
  Ads7950 = 0, ///< 4 channels.
  Ads7951 = 1, ///< 8 channels.
  Ads7952 = 2, ///< 12 channels. The default.
  Ads7953 = 3, ///< 16 channels.
};

/** @brief The package: the VQFN parts bring out GPIO0 only (section 8.5.2). */
enum class Package : uint8_t {
  Tssop = 0, ///< TSSOP (DBT): GPIO0 to GPIO3. The default.
  Vqfn = 1,  ///< VQFN (RGE, RHB): GPIO0 only; the driver programs GPIO1 to GPIO3 as outputs, as
             ///< Table 11 requires.
};

/** @brief Input range, DI06 of the mode control word (Tables 1, 2, 5; Table 8). */
enum class Range : uint8_t {
  Vref = 0,    ///< 0 to VREF (Range 1). The reset value.
  TwoVref = 1, ///< 0 to 2 x VREF (Range 2); specified only while 2 x VREF <= +VA (section 7.6).
};

namespace detail {
/// `Variant` is contiguous from 0 (`IsEnumerator()`).
template <>
struct EnumLast<Variant> {
  static constexpr Variant kValue = Variant::Ads7953; ///< The last enumerator.
};
/// `Package` is contiguous from 0 (`IsEnumerator()`).
template <>
struct EnumLast<Package> {
  static constexpr Package kValue = Package::Vqfn; ///< The last enumerator.
};
/// `Range` is contiguous from 0 (`IsEnumerator()`).
template <>
struct EnumLast<Range> {
  static constexpr Range kValue = Range::TwoVref; ///< The last enumerator.
};
} // namespace detail

/**
 * @brief Extra verification for one call, chosen per call (handbook KB-ARCH-041).
 *
 * The datasheet needs none of it: `Verify::None`, the default, sends only the frames the chip
 * needs. `Verify::ReadBack` costs frames on the bus; each call that takes it says how many.
 */
enum class Verify : uint8_t {
  None = 0,     ///< No extra frame. The default.
  ReadBack = 1, ///< Read back what the chip took (GPIO levels, the scanned sequence).
};

/**
 * @brief What GPIO0 and GPIO1 do besides general-purpose I/O: DI06:04 of the GPIO program register
 * (Table 11). Alarm outputs are active high.
 */
enum class AlarmOutput : uint8_t {
  None = 0,              ///< 000: GPIO0 and GPIO1 are general-purpose I/O. The reset value.
  Gpio0HighOrLow = 1,    ///< xx1: GPIO0 is the "high or low" alarm output.
  Gpio0High = 2,         ///< 010: GPIO0 is the high alarm output.
  Gpio1Low = 4,          ///< 100: GPIO1 is the low alarm output (TSSOP only).
  Gpio1LowGpio0High = 6, ///< 110: GPIO1 the low and GPIO0 the high alarm output (TSSOP only).
};

/**
 * @brief The GPIO program register and the GPIO output levels. `GpioConfig{}` is the reset state:
 * every pin an input with no function.
 *
 * A pin given a function (an alarm output, the range or power-down input) ignores its direction
 * bit, so it must not be in `output_mask` (Table 11 note).
 */
struct GpioConfig {
  uint8_t output_mask{0};   ///< Bit n = 1: GPIOn is a general-purpose output (DI03:00).
  uint8_t output_levels{0}; ///< Levels of the output pins, bit n for GPIOn; only output pins.
  AlarmOutput alarm_output{AlarmOutput::None}; ///< GPIO0/GPIO1 alarm functions (DI06:04).
  bool gpio2_range_input{false};      ///< GPIO2 selects the range (DI07); DI06 is then ignored.
  bool gpio3_power_down_input{false}; ///< GPIO3 is the active-low power-down input (DI08).
};

/**
 * @brief The two alarm thresholds of one channel, in 12-bit result codes. The chip keeps the upper
 * 10 bits and compares them with the upper 10 bits of each result (Table 13), so the two lowest
 * bits of a threshold are dropped. `AlarmThreshold{}` is the reset state: no alarm.
 */
struct AlarmThreshold {
  uint16_t low_code{0};     ///< Low alarm when the result is below this; 0 to 4095.
  uint16_t high_code{4095}; ///< High alarm when the result is above this; 0 to 4095.
};

/// Channels of the largest part (ADS7953); arrays indexed by channel have this size.
inline constexpr std::size_t kMaxChannels = 16;
/// Largest result code: 12 bits, straight binary (Table 8).
inline constexpr uint16_t kMaxCode = 4095;
/// Codes per full scale: one LSB is full scale / 4096 (Table 8).
inline constexpr uint32_t kCodesPerFullScale = 4096;
/// Longest wait after the chip leaves power-down or reset before its first conversion, in
/// microseconds (section 7.6, "Power-up time").
inline constexpr uint32_t kPowerUpUs = 1;
/// Longest `TraceFrames()` request, in frames.
inline constexpr std::size_t kMaxTraceFrames = 32;
/// Bytes per transfer: every frame is 16 bits with CS low throughout (section 8.1).
inline constexpr std::size_t kFrameBytes = 2;

/**
 * @brief Number of analog inputs of a variant.
 * @param variant The part.
 * @return 4, 8, 12 or 16; 0 for a value that is not an enumerator.
 */
[[nodiscard]] constexpr uint8_t ChannelCount(Variant variant) noexcept {
  switch (variant) {
  case Variant::Ads7950:
    return 4;
  case Variant::Ads7951:
    return 8;
  case Variant::Ads7952:
    return 12;
  case Variant::Ads7953:
    return 16;
  default:
    break;
  }
  return 0;
}

/**
 * @brief The mask of every channel of a variant: also the reset value of its Auto-1 program
 * register (section 8.4.1).
 * @param variant The part.
 * @return 0x000F, 0x00FF, 0x0FFF or 0xFFFF; 0 for a value that is not an enumerator.
 */
[[nodiscard]] constexpr uint16_t AllChannels(Variant variant) noexcept {
  // ChannelCount() is 0, 4, 8, 12 or 16: the shift stays below the width of uint32_t.
  return static_cast<uint16_t>((uint32_t{1} << ChannelCount(variant)) - 1U);
}

static_assert(kMaxChannels <= 16U, "channel masks are 16 bits");
static_assert((AllChannels(Variant::Ads7950) == 0x000FU) &&
              (AllChannels(Variant::Ads7953) == 0xFFFFU));

/**
 * @brief The result of one Auto-1 or Auto-2 scan: one code per channel of the sequence.
 *
 * Every channel in `mask` was received exactly once, in one pass of the sequence; the other
 * entries of `code` are 0.
 */
struct Bank {
  std::array<uint16_t, kMaxChannels> code{}; ///< Result of channel n at index n, 0 to 4095.
  uint16_t mask{0};                          ///< The channels of the scan, bit n for channel n.
  /// The range every code of the scan was converted in, so a bank kept across `SetRange()` still
  /// converts with its own scale (`ADS7952::ToMicrovolts(const Bank&, uint8_t)`).
  Range range{Range::Vref};

  /**
   * @param channel 0 to 15.
   * @return true when the scan holds `channel`.
   */
  [[nodiscard]] constexpr bool Has(uint8_t channel) const noexcept {
    return (channel < kMaxChannels) && (((static_cast<uint32_t>(mask) >> channel) & 1U) != 0U);
  }
};

/// `ScaleMonitor::channel` when no channel carries a known voltage: no scale check.
inline constexpr uint8_t kNoMonitor = 0xFF;

/// `Config::park_channel` when the chip stays on channel 0 after a read of it: no parking.
inline constexpr uint8_t kNoPark = 0xFF;

/**
 * @brief A channel the board drives with a known voltage, so the driver can prove the scale every
 * code is converted with: the range the chip is really in, and a reference that is present.
 *
 * The chip has no way to read its range back (section 8.4.2) and powers up and resets in Range 1,
 * so a lost range doubles or halves every result while every channel address still checks out.
 * A known input tells the two apart: in the other range it reads twice or half the code. Best is
 * a voltage derived from REFP itself (REFP, or REFP through two matched resistors): its code does
 * not depend on the reference's accuracy. `ScaleMonitor{}` is no monitor.
 */
struct ScaleMonitor {
  uint8_t channel{kNoMonitor}; ///< The channel, or `kNoMonitor`.
  uint32_t expected_uv{0};     ///< Its nominal input, in microvolts; strictly inside the range.
  uint32_t tolerance_uv{0};    ///< Accepted deviation, in microvolts; the windows of the two
                               ///< ranges must not touch (`Validate()` checks it).
};

/**
 * @brief What the driver is told about the chip and the board. `Config{}` is an ADS7952 in TSSOP
 * left in its reset state (Tables 1 to 13): Range 1, every GPIO an input, every channel in Auto-1,
 * channel 11 the last of Auto-2, no alarm; with a 2.5 V reference and a 5 V analog supply.
 *
 * Every setting is written by `EnsureInitialized()`, which first resets the chip, and by
 * `ApplyConfig()` on a running driver. Verification is not configured here: each call that can
 * read back takes a `Verify` argument.
 */
struct Config {
  Variant variant{Variant::Ads7952}; ///< The fitted part; every channel is checked against it.
  Package package{Package::Tssop};   ///< The package; VQFN has GPIO0 only.
  /// Input range, sent with every mode word (DI06). With `gpio.gpio2_range_input` the chip ignores
  /// DI06 and the GPIO2 pin selects the range: then this is the level the board drives on GPIO2,
  /// which the driver cannot see; declare a `monitor` to check it.
  Range range{Range::Vref};
  /// REFP voltage, in millivolts: 2000 to 3000 (section 7.3). Sets the scale of every code (one
  /// LSB is `vref_mv` / 4096 in Range 1, 2 x `vref_mv` / 4096 in Range 2) and is checked against
  /// `va_min_mv`; never sent to the chip.
  uint16_t vref_mv{2500};
  /// The lowest +VA the board guarantees, in millivolts: the rail's minimum after its tolerance,
  /// ripple and load, not its nominal value. 2700 to 5250 (section 7.3). Range 2 is specified only
  /// while 2 x VREF <= +VA (section 7.6), so it needs 2 x `vref_mv` <= `va_min_mv`.
  uint16_t va_min_mv{5000};
  /// The Auto-1 sequence, bit n for channel n; at least one channel of the variant. The reset
  /// value of an ADS7952 is 0x0FFF (every channel).
  uint16_t auto1_channels{0x0FFF};
  /// The last channel of the Auto-2 sequence (channel 0 to this one). Reset value: the variant's
  /// last channel (11 on the ADS7952).
  uint8_t auto2_last_channel{11};
  GpioConfig gpio{};                                 ///< GPIO program register and output levels.
  std::array<AlarmThreshold, kMaxChannels> alarms{}; ///< Thresholds per channel; index = channel.
  /// A channel with a known voltage: checked by `EnsureInitialized()`, `CheckScale()` and
  /// `CheckScaleCode()`. None by default.
  ScaleMonitor monitor{};
  /// Reset witness for reads of channel 0 (opt-in): 1 to the variant's last channel, or `kNoPark`.
  /// Channel 0 is the channel a reset leaves the chip on (section 8.4.3), so a reset while the chip
  /// rests there shows in no reply. With a park channel, `ReadChannel(0)` reads with the Manual
  /// word of the park channel and one frame more, so the chip rests there; the next read checks
  /// that the chip is still on it and returns `DeviceReset` if a reset moved it to channel 0. Cost:
  /// `ReadChannel(0)` takes 4 frames instead of 1 when repeated, 2 instead of 1 the first time.
  uint8_t park_channel{kNoPark};
};

namespace detail {

/// The full scale of a range, in microvolts: VREF or 2 x VREF (Table 8).
[[nodiscard]] constexpr uint32_t FullScaleUv(Range range, uint16_t vref_mv) noexcept {
  return static_cast<uint32_t>(vref_mv) * ((range == Range::TwoVref) ? 2000U : 1000U);
}

/// The code an input converts to, clipped to 4095 (Table 8: one LSB is full scale / 4096).
[[nodiscard]] constexpr uint32_t CodeOfInput(uint32_t input_uv, uint32_t full_scale_uv) noexcept {
  const uint64_t code = (static_cast<uint64_t>(input_uv) * 4096U) / full_scale_uv;
  return (code > 4095U) ? 4095U : static_cast<uint32_t>(code);
}

/// Where the scale monitor's code must fall, in codes of the configured range.
struct MonitorWindow {
  uint32_t expected; ///< The monitor's code in `Config::range`.
  uint32_t other;    ///< Its code if the chip converts in the other range.
  uint32_t margin;   ///< The tolerance, in codes, rounded up.
};

[[nodiscard]] constexpr MonitorWindow WindowOf(const Config& config) noexcept {
  const Range other = (config.range == Range::TwoVref) ? Range::Vref : Range::TwoVref;
  const uint32_t fs = FullScaleUv(config.range, config.vref_mv);
  const uint64_t margin =
      ((static_cast<uint64_t>(config.monitor.tolerance_uv) * 4096U) + fs - 1U) / fs;
  return {.expected = CodeOfInput(config.monitor.expected_uv, fs),
          .other = CodeOfInput(config.monitor.expected_uv, FullScaleUv(other, config.vref_mv)),
          .margin = (margin > 4096U) ? 4096U : static_cast<uint32_t>(margin)};
}

[[nodiscard]] constexpr bool Within(uint32_t code, uint32_t centre, uint32_t margin) noexcept {
  return ((code + margin) >= centre) && (code <= (centre + margin));
}

} // namespace detail

/**
 * @brief Check the scale monitor of a configuration.
 * @param config The configuration.
 * @return `Ok` with no monitor or a usable one; `InvalidArgument` for a channel the variant lacks,
 *         a zero tolerance, a window that touches code 0 or 4095 (a dead or clipped input would
 *         pass), or windows of the two ranges that touch (a lost range could pass).
 */
[[nodiscard]] constexpr Error ValidateMonitor(const Config& config) noexcept {
  const ScaleMonitor& m = config.monitor;
  if (m.channel == kNoMonitor) {
    return Error::Ok;
  }
  if ((m.channel >= ChannelCount(config.variant)) || (m.tolerance_uv == 0U)) {
    return Error::InvalidArgument;
  }
  const detail::MonitorWindow w = detail::WindowOf(config);
  if ((w.expected <= w.margin) || ((w.expected + w.margin) >= kMaxCode)) {
    return Error::InvalidArgument;
  }
  const uint32_t gap = (w.expected > w.other) ? (w.expected - w.other) : (w.other - w.expected);
  return (gap > (2U * w.margin)) ? Error::Ok : Error::InvalidArgument;
}

/**
 * @brief Judge one code of the scale monitor's channel. No I/O.
 * @param config The configuration (its `monitor`, `range` and `vref_mv`).
 * @param code The monitor channel's code, from any read, `Bank` or DMA stream converted in
 *        `config.range`.
 * @return `Ok`; `RangeMismatch` when the code is what the other range gives; `ScaleMismatch` when
 *         it is in neither window; `InvalidArgument` with no monitor or an invalid one.
 */
[[nodiscard]] constexpr Error CheckScaleCode(const Config& config, uint16_t code) noexcept {
  if ((config.monitor.channel == kNoMonitor) || (ValidateMonitor(config) != Error::Ok)) {
    return Error::InvalidArgument;
  }
  const detail::MonitorWindow w = detail::WindowOf(config);
  const uint32_t c = static_cast<uint32_t>(code) & kMaxCode;
  if (detail::Within(c, w.expected, w.margin)) {
    return Error::Ok;
  }
  return detail::Within(c, w.other, w.margin) ? Error::RangeMismatch : Error::ScaleMismatch;
}

/**
 * @brief Check the GPIO settings of a configuration against its package.
 * @param config The configuration.
 * @return `Ok`, `InvalidArgument` or `Unsupported` (a pin or function the VQFN package lacks).
 */
[[nodiscard]] constexpr Error ValidateGpio(const Config& config) noexcept {
  const GpioConfig& g = config.gpio;
  const auto alarm = static_cast<uint32_t>(detail::ToUnderlying(g.alarm_output));
  const uint32_t outputs = g.output_mask;
  const uint32_t levels = g.output_levels;
  if ((alarm > 6U) || (alarm == 3U) || (alarm == 5U) || (outputs > 0x0FU) ||
      ((levels & ~outputs) != 0U)) {
    return Error::InvalidArgument;
  }
  // A pin with a function ignores its direction bit (Table 11 note): it cannot be an output. The
  // bits collect in uint32_t, so no compound assignment narrows (KB-CPP20-022).
  uint32_t function_pins = 0U;
  function_pins |= ((alarm & 0x03U) != 0U) ? 0x01U : 0x00U; // GPIO0: any code with bit 0 or bit 1
  function_pins |= ((alarm & 0x04U) != 0U) ? 0x02U : 0x00U; // GPIO1: codes 100 and 110
  function_pins |= g.gpio2_range_input ? 0x04U : 0x00U;
  function_pins |= g.gpio3_power_down_input ? 0x08U : 0x00U;
  if ((function_pins & outputs) != 0U) {
    return Error::InvalidArgument;
  }
  if ((config.package == Package::Vqfn) && (((function_pins | outputs) & 0x0EU) != 0U)) {
    return Error::Unsupported;
  }
  return Error::Ok;
}

/**
 * @brief Check the alarm thresholds of a configuration against its variant.
 * @param config The configuration.
 * @return `Ok`, or `InvalidArgument` for a code above 4095 or a threshold other than the reset
 *         value on a channel the variant lacks.
 */
[[nodiscard]] constexpr Error ValidateAlarms(const Config& config) noexcept {
  const uint8_t channels = ChannelCount(config.variant);
  for (std::size_t ch = 0; ch < kMaxChannels; ++ch) {
    const AlarmThreshold& a = config.alarms[ch];
    const bool reset = (a.low_code < 4U) && (a.high_code >= 4092U);
    if ((a.low_code > kMaxCode) || (a.high_code > kMaxCode) || ((ch >= channels) && !reset)) {
      return Error::InvalidArgument;
    }
  }
  return Error::Ok;
}

/**
 * @brief Check the reference and the analog supply (section 7.3), and Range 2 against them
 * (2 x VREF <= +VA, section 7.6).
 * @param config The configuration.
 * @return true when `vref_mv`, `va_min_mv` and `range` fit together.
 */
[[nodiscard]] constexpr bool ValidSupplies(const Config& config) noexcept {
  if ((config.vref_mv < 2000U) || (config.vref_mv > 3000U) || (config.va_min_mv < 2700U) ||
      (config.va_min_mv > 5250U)) {
    return false;
  }
  return (config.range != Range::TwoVref) ||
         ((2U * static_cast<uint32_t>(config.vref_mv)) <= static_cast<uint32_t>(config.va_min_mv));
}

/**
 * @brief Check `Config::park_channel` against the variant.
 * @param config The configuration.
 * @return true for `kNoPark` or a channel from 1 to the variant's last.
 */
[[nodiscard]] constexpr bool ValidPark(const Config& config) noexcept {
  return (config.park_channel == kNoPark) ||
         ((config.park_channel != 0U) && (config.park_channel < ChannelCount(config.variant)));
}

/**
 * @brief Check a configuration without touching the chip.
 * @param config The configuration.
 * @return `Ok`; `InvalidArgument` for a value that is not an enumerator, a voltage outside section
 *         7.3, Range 2 with 2 x VREF above +VA, a channel the variant lacks, a threshold above
 *         4095, a scale monitor `ValidateMonitor()` refuses, a park channel `ValidPark()`
 *         refuses, or a GPIO setting that contradicts itself; `Unsupported` for a GPIO the package
 * lacks.
 */
[[nodiscard]] constexpr Error Validate(const Config& config) noexcept {
  const uint8_t channels = ChannelCount(config.variant);
  if (!detail::IsEnumerator(config.variant) || !detail::IsEnumerator(config.package) ||
      !detail::IsEnumerator(config.range)) {
    return Error::InvalidArgument;
  }
  if (!ValidSupplies(config)) {
    return Error::InvalidArgument;
  }
  const auto all = static_cast<uint32_t>(AllChannels(config.variant));
  if ((config.auto1_channels == 0U) ||
      ((static_cast<uint32_t>(config.auto1_channels) & ~all) != 0U) ||
      (config.auto2_last_channel >= channels)) {
    return Error::InvalidArgument;
  }
  if ((ValidateAlarms(config) != Error::Ok) || (ValidateMonitor(config) != Error::Ok)) {
    return Error::InvalidArgument;
  }
  if (!ValidPark(config)) {
    return Error::InvalidArgument;
  }
  return ValidateGpio(config);
}

/**
 * @brief The `Config` of a variant left in its reset state: `Config{}` with that variant's Auto-1
 * mask and Auto-2 last channel.
 * @param variant The part.
 * @return The configuration.
 */
[[nodiscard]] constexpr Config ResetConfig(Variant variant) noexcept {
  Config c{};
  c.variant = variant;
  c.auto1_channels = AllChannels(variant);
  const auto channels = static_cast<uint32_t>(ChannelCount(variant));
  c.auto2_last_channel = static_cast<uint8_t>((channels > 0U) ? (channels - 1U) : 0U);
  return c;
}

// The reset configuration of each part is valid, and `Config{}` is the ADS7952's (section 8.4.3).
static_assert(Validate(Config{}) == Error::Ok);
static_assert((Validate(ResetConfig(Variant::Ads7950)) == Error::Ok) &&
              (Validate(ResetConfig(Variant::Ads7951)) == Error::Ok) &&
              (Validate(ResetConfig(Variant::Ads7953)) == Error::Ok));
static_assert((ResetConfig(Variant::Ads7952).auto1_channels == Config{}.auto1_channels) &&
              (ResetConfig(Variant::Ads7952).auto2_last_channel == Config{}.auto2_last_channel));

/**
 * @brief Convert a result code to the input voltage it stands for (Table 8: one LSB is full scale
 * / 4096, code 4095 is full scale less one LSB).
 * @param code 0 to 4095.
 * @param range The range the conversion was made in.
 * @param vref_mv The reference, in millivolts.
 * @return The input voltage at the bottom of the code's step, in microvolts.
 */
[[nodiscard]] constexpr uint32_t CodeToMicrovolts(uint16_t code, Range range,
                                                  uint16_t vref_mv) noexcept {
  const uint32_t full_scale_uv = detail::FullScaleUv(range, vref_mv);
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(static_cast<uint32_t>(code) & kMaxCode) * full_scale_uv) /
      kCodesPerFullScale);
}

// Table 8: code 4095 is full scale less one LSB; one LSB of Range 1 at 2.5 V is 610 uV.
using detail::operator""_u16;
static_assert(CodeToMicrovolts(4095_u16, Range::Vref, 2500_u16) == 2499389U);
static_assert((CodeToMicrovolts(1_u16, Range::Vref, 2500_u16) == 610U) &&
              (CodeToMicrovolts(1_u16, Range::TwoVref, 2500_u16) == 1220U));

/** @return The driver version, e.g. `"0.1.0"`, from the generated `ads7952_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return ADS7952_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return ADS7952_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return ADS7952_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return ADS7952_VERSION_PATCH;
}

/** @} */

} // namespace ads7952

#endif // ADS7952_TYPES_HPP
