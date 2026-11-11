/**
 * @file mock_ads7952.hpp
 * @brief A protocol model of the ADS7950, ADS7951, ADS7952 and ADS7953 for the host tests, plus the
 * mock ports in mock_ads7952_bus.hpp.
 *
 * The model runs the chip frame by frame as datasheet SLAS605C describes it (sections 8.1 and 8.4,
 * Figures 1, 50 to 56, Tables 1 to 13):
 *
 * - at each CS falling edge it samples the channel the multiplexer acquired during the previous
 *   frame, in the range that applied then, and shifts out its address (or, after DI04 = 1, the
 *   GPIO levels latched at that edge) and its 12-bit code; GPIO outputs take the levels written in
 *   the previous frame on the same edge (the model latches the inputs first, the conservative
 *   reading of "on the CS falling edge");
 * - during the frame the multiplexer switches: Manual to the channel the previous frame chose,
 *   Auto-1 to the next enabled channel (the first one after an entry or a reset of the sequence),
 *   Auto-2 to the next channel up to the last one;
 * - at the CS rising edge it parses the word: mode words (DI11 gates DI06:00, or DI10:00 in the
 *   Auto modes; "continue" changes nothing), the GPIO program register with its reset bit (the
 *   reset happens in the next frame, whose own word is ignored), the two-frame Auto-1 program,
 *   the Auto-2 program (Auto-2 restarts at channel 0), the alarm sequence (the frame after the
 *   exit bit is ignored, the conservative reading of "exits in the next frame"), power-down at the
 *   end of the frame after DI05 = 1 and wake-up on the next CS falling edge with DI05 = 0;
 * - the first conversion after a reset or a wake-up is invalid, and a frame sent within the
 *   power-up time after one counts as `early_frames`;
 * - the supply can dip on its own in the middle of a session (`brownout_before_frame`): every
 *   register goes back to its reset value (section 8.4.3: Manual mode, channel 0, Range 1, every
 *   GPIO an input with no alarm function, Table 11; the alarm thresholds to all ones and all zeros,
 *   section 8.5.3) and the next conversion is invalid (section 7.6). Nothing in the reply says so.
 *
 * Faults are injected through the public fields; each one is a bus or board condition a real board
 * can produce, or one the datasheet names (docs/limitations.md).
 *
 * The model is written from the datasheet on its own (handbook KB-TST-030): it includes none of
 * the driver's headers, but carries its own frame format, reset values, channel counts and timing
 * below (namespace `chip`, each with its datasheet section), so a wrong constant in the driver
 * cannot hide behind the same wrong constant in the model. tests/test_model.cpp checks the two
 * against each other at compile time.
 */
#ifndef ADS7952_MOCK_ADS7952_HPP
#define ADS7952_MOCK_ADS7952_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ads7952_test {

/// The chip as datasheet SLAS605C describes it, independent of the driver's headers.
namespace chip {
/// The 12-bit parts of the family (section 8.4.1, Table 4).
enum class Part : uint8_t { Ads7950 = 0, Ads7951 = 1, Ads7952 = 2, Ads7953 = 3 };
/// The packages: VQFN brings out GPIO0 only (section 8.5.2).
enum class Package : uint8_t { Tssop = 0, Vqfn = 1 };
/// Analog inputs of a part (Table 4): 4, 8, 12, 16.
[[nodiscard]] constexpr uint8_t ChannelCount(Part part) noexcept {
  return static_cast<uint8_t>(4U * (static_cast<uint32_t>(part) + 1U));
}
// DI15:12 of a command word (Tables 1, 2, 3, 5, 6, 11, 13).
inline constexpr uint32_t kCommandShift = 12;
inline constexpr uint16_t kContinue = 0x0000U;
inline constexpr uint16_t kManual = 0x1000U;
inline constexpr uint16_t kAuto1 = 0x2000U;
inline constexpr uint16_t kAuto2 = 0x3000U;
inline constexpr uint16_t kGpioProgram = 0x4000U;
inline constexpr uint16_t kAuto1Program = 0x8000U;
inline constexpr uint16_t kAuto2Program = 0x9000U;
inline constexpr uint16_t kAlarmProgramGroup0 = 0xC000U; // 11bb, bb the group (Table 13)
// Mode control word (Tables 1, 2, 5).
inline constexpr uint16_t kProgramEnable = 0x0800U; // DI11
inline constexpr uint16_t kAutoReset = 0x0400U;     // DI10, Auto modes
inline constexpr uint32_t kManualChannelShift = 7;  // DI10:07, Manual
inline constexpr uint16_t kRangeTwoVref = 0x0040U;  // DI06
inline constexpr uint16_t kPowerDown = 0x0020U;     // DI05
inline constexpr uint16_t kGpioReadout = 0x0010U;   // DI04
inline constexpr uint16_t kGpioDataMask = 0x000FU;  // DI03:00
// Auto-2 program register (Table 6).
inline constexpr uint32_t kAuto2LastShift = 6; // DI09:06
// GPIO program register (Table 11).
inline constexpr uint16_t kGpioReservedMask = 0x0C00U; // DI11:10, must be 00
inline constexpr uint16_t kGpioResetAll = 0x0200U;     // DI09
inline constexpr uint16_t kGpio3PowerDown = 0x0100U;   // DI08
inline constexpr uint16_t kGpio2Range = 0x0080U;       // DI07
inline constexpr uint32_t kGpioAlarmShift = 4;         // DI06:04
inline constexpr uint16_t kGpioRegisterMask = 0x01FFU; // DI08:00
// Alarm program register, frame 2 onwards (Table 13).
inline constexpr uint32_t kAlarmChannelShift = 14;       // DI15:14
inline constexpr uint16_t kAlarmHigh = 0x2000U;          // DI13
inline constexpr uint16_t kAlarmExit = 0x1000U;          // DI12
inline constexpr uint16_t kAlarmThresholdMask = 0x03FFU; // DI09:00
inline constexpr uint16_t kAlarmHighReset = 0x03FFU;     // section 8.5.3: reset value all ones
inline constexpr uint16_t kAlarmLowReset = 0x0000U;      // ... and all zeros
// Reply word (section 8.1, Figure 2).
inline constexpr uint32_t kReplyAddressShift = 12;  // DO15:12
inline constexpr uint16_t kReplyCodeMask = 0x0FFFU; // DO11:00
inline constexpr uint32_t kFrameBytes = 2;          // 16 SCLK with CS low (section 8.1)
// Timing (section 7.6): the power-up time after a reset or a wake-up.
inline constexpr uint32_t kPowerUpUs = 1;
} // namespace chip

/// What MISO carries, whatever the chip does.
enum class Miso : uint8_t {
  Chip = 0,  ///< The chip drives SDO: normal operation.
  FloatHigh, ///< No chip on the CS (board unplugged or unpowered), MISO pulled high: 0xFFFF.
  StuckLow,  ///< MISO floats low or is shorted low: 0x0000.
  Pattern,   ///< MISO carries the same word every frame (`pattern`), e.g. 0x0E83.
  EchoMosi,  ///< MISO repeats MOSI one byte late (SDI shorted to SDO, a loop-back).
  Noise,     ///< Every word random: a contended or broken MISO.
};

class Ads7952Model {
public:
  // --- What the chip is -------------------------------------------------------------------------
  chip::Part variant = chip::Part::Ads7952;     ///< The fitted part.
  chip::Package package = chip::Package::Tssop; ///< The package.
  uint16_t vref_mv = 2500;                      ///< REFP.
  std::array<uint16_t, 16> input_mv{};          ///< Analog input of each channel.
  uint8_t gpio_inputs = 0; ///< Levels the board drives on the GPIO pins that are inputs.
  uint8_t gpio_held = 0;   ///< Output pins the board holds at their `gpio_inputs` level.

  // --- Faults
  // -------------------------------------------------------------------------------------
  Miso miso = Miso::Chip;    ///< What MISO carries.
  uint16_t pattern = 0x0E83; ///< The word of `Miso::Pattern`.
  bool deaf_sdi = false;     ///< SDI does not reach the chip: it sees 0x0000 in every frame.
  /// What a brown-out does to the conversion in flight.
  enum class Pipeline : uint8_t {
    Kept, ///< It survives: the next reply carries its address with an invalid code.
    Lost, ///< It is lost too: the next reply is the invalid first conversion of channel 0.
  };
  int brownout_before_frame = -1; ///< The supply dips before this frame (0-based count): a reset
                                  ///< the chip does on its own, between two frames of the driver.
  Pipeline brownout_pipeline =
      Pipeline::Kept;             ///< ... with this effect on the conversion in flight.
  uint32_t noise_state = 0x1234U; ///< Seed of `Miso::Noise`.

  // --- State (read it; set it only to stage a test) ----------------------------------------------
  enum class Mode : uint8_t { Manual, Auto1, Auto2 };
  Mode mode = Mode::Manual;              ///< Channel sequencing mode.
  uint8_t manual_next = 0;               ///< DI10:07 of the last Manual word.
  uint8_t acquiring = 0;                 ///< Channel the multiplexer acquires in this frame.
  bool acquiring_two_vref = false;       ///< Range of that acquisition.
  bool two_vref = false;                 ///< DI06.
  bool power_down_bit = false;           ///< DI05.
  bool powered_down = false;             ///< The chip is powered down.
  bool gpio_readout = false;             ///< DI04.
  uint8_t gpo = 0;                       ///< Levels the GPIO outputs drive.
  uint16_t gpio_register = 0;            ///< GPIO program register, DI08:00.
  uint16_t auto1_mask = 0x0FFF;          ///< Auto-1 program register.
  uint8_t auto2_last = 11;               ///< Auto-2 program register.
  std::array<uint16_t, 16> alarm_low{};  ///< Low thresholds (10 bits).
  std::array<uint16_t, 16> alarm_high{}; ///< High thresholds (10 bits).
  bool alarm_high_out = false;           ///< The high alarm of the last conversion.
  bool alarm_low_out = false;            ///< The low alarm of the last conversion.

  // --- Counters
  // ------------------------------------------------------------------------------------
  int frames = 0;         ///< Frames clocked.
  int resets = 0;         ///< Register resets done.
  int ignored_words = 0;  ///< Words ignored (after a reset or an alarm exit).
  int reserved_words = 0; ///< Words with a reserved command or a group the part lacks.
  int bad_gpio_words = 0; ///< GPIO program words with DI11:10 not 00 (Table 11 forbids it).
  int early_frames = 0;   ///< Frames sent within the power-up time after a reset or wake-up.

  Ads7952Model() noexcept {
    for (std::size_t ch = 0; ch < input_mv.size(); ++ch) {
      input_mv[ch] = static_cast<uint16_t>(100U + (150U * ch)); // every channel different
    }
    resetRegisters();
    resets = 0;           // powered up long before the test starts
    wait_needed_ = false; // ... and its power-up time is over
  }

  [[nodiscard]] uint8_t Channels() const noexcept {
    return chip::ChannelCount(variant);
  }

  /// Power lost and back (a brown-out): every register to its reset value, and the next
  /// conversion invalid (section 7.6). With `Pipeline::Lost` the conversion in flight is gone too
  /// and the next reply is the first conversion of channel 0, the power-on channel (section 8.4.3).
  void Brownout(Pipeline pipeline = Pipeline::Kept) noexcept {
    resetRegisters();
    powered_down = false;
    reset_pending_ = false;
    ignore_word_ = false;
    wait_needed_ = false; // the supply came back before the next frame
    if (pipeline == Pipeline::Lost) {
      acquiring = 0;
      acquiring_two_vref = false;
      sampled_ = 0;
      sampled_two_vref_ = false;
      sampled_next_ = 0;
      sampled_next_two_vref_ = false;
    }
  }

  /// Simulated time passes (the port's DelayUs()).
  void Elapse(uint32_t us) noexcept {
    if (us >= chip::kPowerUpUs) {
      wait_needed_ = false;
    }
  }

  /// The 12-bit code `channel` converts to in a range.
  [[nodiscard]] uint16_t CodeOf(uint8_t channel, bool range_two_vref) const noexcept {
    if (channel >= Channels()) {
      return 0;
    }
    const uint32_t full_scale = static_cast<uint32_t>(vref_mv) * (range_two_vref ? 2U : 1U);
    const uint32_t code = (static_cast<uint32_t>(input_mv[channel]) * 4096U) / full_scale;
    return static_cast<uint16_t>(code > 4095U ? 4095U : code);
  }

  /// The levels on GPIO3:0 now.
  [[nodiscard]] uint8_t PinLevels() const noexcept {
    const auto alarm = static_cast<uint8_t>(
        (static_cast<uint32_t>(gpio_register) >> chip::kGpioAlarmShift) & 0x07U);
    const bool qfn = package == chip::Package::Vqfn;
    uint8_t levels = 0;
    for (uint8_t pin = 0; pin < 4; ++pin) {
      const auto bit = static_cast<uint8_t>(1U << pin);
      bool level = (gpio_inputs & bit) != 0U;
      const bool output = (gpio_register & bit) != 0U;
      if (pin == 0 && (alarm & 0x03U) != 0U) {
        level = (alarm & 0x01U) != 0U ? (alarm_high_out || alarm_low_out) : alarm_high_out;
      } else if (pin == 1 && (alarm & 0x04U) != 0U) {
        level = alarm_low_out;
      } else if (output && (gpio_held & bit) == 0U) {
        level = (gpo & bit) != 0U;
      }
      levels |= level && (!qfn || pin == 0) ? bit : 0U;
    }
    return levels;
  }

  /// One frame: `word` in on SDI while the reply goes out on SDO.
  uint16_t Frame(uint16_t word) noexcept {
    if (frames == brownout_before_frame) {
      Brownout(brownout_pipeline);
    }
    frames++;
    if (wait_needed_) {
      early_frames++;
    }
    const uint16_t in = deaf_sdi ? uint16_t{0} : word;
    const uint16_t reply = fallingEdge();
    muxSwitch();
    risingEdge(in);
    return busMiso(word, reply);
  }

private:
  enum class Program : uint8_t { None, Auto1Mask, Alarm, AlarmExit };

  void resetRegisters() noexcept {
    mode = Mode::Manual;
    manual_next = 0;
    two_vref = false;
    power_down_bit = false;
    gpio_readout = false;
    gpo = 0;
    gpo_pending_ = 0;
    gpio_register = 0;
    auto1_mask = allChannels();
    auto2_last = static_cast<uint8_t>(Channels() - 1U);
    alarm_low.fill(chip::kAlarmLowReset);
    alarm_high.fill(chip::kAlarmHighReset);
    program_ = Program::None;
    restart_ = false;
    power_down_armed_ = false;
    invalid_ = 1; // the first conversion after power-up or reset (section 7.6)
    wait_needed_ = true;
    resets++;
  }

  uint16_t fallingEdge() noexcept {
    if (reset_pending_) {
      reset_pending_ = false;
      resetRegisters();
      ignore_word_ = true;
    }
    if (powered_down && !power_down_bit && !gpio3PowerDownLow()) {
      powered_down = false; // section 8.3.2: power up on the CS falling edge with DI05 = 0
      invalid_ = 1;
      wait_needed_ = true;
    }
    const uint8_t latched = PinLevels(); // inputs latched, then the outputs refreshed
    gpo = gpo_pending_;
    uint16_t code = powered_down ? uint16_t{0} : CodeOf(sampled_, sampled_two_vref_);
    if (invalid_ > 0) {
      invalid_--;
      code = 0x0A5A; // an invalid conversion: any value
    }
    const auto upper = static_cast<uint16_t>(code >> 2U);
    alarm_high_out = sampled_ < Channels() && upper > alarm_high[sampled_];
    alarm_low_out = sampled_ < Channels() && upper < alarm_low[sampled_];
    const uint8_t address = gpio_readout ? latched : sampled_;
    return static_cast<uint16_t>((static_cast<unsigned>(address & 0x0FU) << 12U) | code);
  }

  void muxSwitch() noexcept {
    // The sample taken at this frame's CS falling edge was the acquisition of the previous frame.
    switch (mode) {
    case Mode::Manual:
      acquiring = manual_next;
      break;
    case Mode::Auto1:
      acquiring = restart_ ? firstOf(auto1_mask) : nextOf(auto1_mask, acquiring);
      break;
    case Mode::Auto2:
      acquiring =
          restart_ || acquiring >= auto2_last ? uint8_t{0} : static_cast<uint8_t>(acquiring + 1U);
      break;
    }
    restart_ = false;
    acquiring_two_vref = gpio2RangeInput() ? (PinLevels() & 0x04U) != 0U : two_vref;
    sampled_next_ = acquiring;
    sampled_next_two_vref_ = acquiring_two_vref;
  }

  void risingEdge(uint16_t in) noexcept {
    sampled_ = sampled_next_;
    sampled_two_vref_ = sampled_next_two_vref_;
    if (power_down_armed_) {
      power_down_armed_ = false;
      powered_down = power_down_bit; // the 16th SCLK of the frame after DI05 = 1
    }
    if (ignore_word_) {
      ignore_word_ = false;
      ignored_words++;
      return;
    }
    if (program_ != Program::None) {
      programFrame(in);
      return;
    }
    command(in);
  }

  void programFrame(uint16_t in) noexcept {
    switch (program_) {
    case Program::Auto1Mask:
      auto1_mask = static_cast<uint16_t>(in & allChannels());
      program_ = Program::None;
      return;
    case Program::Alarm: {
      const std::size_t index = (static_cast<std::size_t>(alarm_group_) * 4U) + (in >> 14U);
      auto& registers = (in & chip::kAlarmHigh) != 0U ? alarm_high : alarm_low;
      registers[index] = static_cast<uint16_t>(in & chip::kAlarmThresholdMask);
      program_ = (in & chip::kAlarmExit) != 0U ? Program::AlarmExit : Program::Alarm;
      return;
    }
    case Program::AlarmExit:
    case Program::None:
      break;
    }
    program_ = Program::None;
    ignored_words++;
  }

  void command(uint16_t in) noexcept {
    const auto cmd = static_cast<uint8_t>(in >> chip::kCommandShift);
    switch (cmd) {
    case 0x0:
      return; // continue
    case 0x1:
      mode = Mode::Manual;
      manual_next =
          static_cast<uint8_t>((static_cast<uint32_t>(in) >> chip::kManualChannelShift) & 0x0FU);
      controlBits(in);
      return;
    case 0x2:
    case 0x3: {
      const Mode next = cmd == 0x2 ? Mode::Auto1 : Mode::Auto2;
      restart_ = restart_ || mode != next ||
                 ((in & chip::kProgramEnable) != 0U && (in & chip::kAutoReset) != 0U);
      mode = next;
      controlBits(in);
      return;
    }
    case 0x4:
      gpioProgram(in);
      return;
    case 0x8:
      program_ = Program::Auto1Mask;
      return;
    case 0x9:
      auto2_last =
          static_cast<uint8_t>((static_cast<uint32_t>(in) >> chip::kAuto2LastShift) & 0x0FU);
      restart_ = restart_ || mode == Mode::Auto2;
      return;
    default:
      break;
    }
    if (cmd >= 0xC && (cmd & 0x03U) < Channels() / 4U) {
      program_ = Program::Alarm;
      alarm_group_ = static_cast<uint8_t>(cmd & 0x03U);
      return;
    }
    reserved_words++;
  }

  void controlBits(uint16_t in) noexcept {
    if ((in & chip::kProgramEnable) == 0U) {
      return; // DI11 = 0: the previous DI06:00 stay
    }
    two_vref = (in & chip::kRangeTwoVref) != 0U;
    power_down_bit = (in & chip::kPowerDown) != 0U;
    power_down_armed_ = power_down_bit;
    gpio_readout = (in & chip::kGpioReadout) != 0U;
    gpo_pending_ = static_cast<uint8_t>(in & chip::kGpioDataMask);
  }

  void gpioProgram(uint16_t in) noexcept {
    if ((in & chip::kGpioReservedMask) != 0U) {
      bad_gpio_words++;
    }
    if ((in & chip::kGpioResetAll) != 0U) {
      reset_pending_ = true;
      return;
    }
    gpio_register = static_cast<uint16_t>(in & chip::kGpioRegisterMask);
  }

  [[nodiscard]] uint16_t allChannels() const noexcept {
    return static_cast<uint16_t>((uint32_t{1} << Channels()) - 1U); // the Auto-1 reset value
  }
  [[nodiscard]] bool gpio2RangeInput() const noexcept {
    return (gpio_register & chip::kGpio2Range) != 0U;
  }
  [[nodiscard]] bool gpio3PowerDownLow() const noexcept {
    return (gpio_register & chip::kGpio3PowerDown) != 0U && (gpio_inputs & 0x08U) == 0U;
  }

  [[nodiscard]] static uint8_t firstOf(uint16_t mask) noexcept {
    for (uint8_t ch = 0; ch < 16; ++ch) {
      if (((static_cast<uint32_t>(mask) >> ch) & 1U) != 0U) {
        return ch;
      }
    }
    return 0;
  }
  [[nodiscard]] static uint8_t nextOf(uint16_t mask, uint8_t current) noexcept {
    for (uint8_t step = 1; step <= 16; ++step) {
      const auto ch = static_cast<uint8_t>((current + step) % 16U);
      if (((static_cast<uint32_t>(mask) >> ch) & 1U) != 0U) {
        return ch;
      }
    }
    return current;
  }

  uint16_t busMiso(uint16_t word, uint16_t reply) noexcept {
    switch (miso) {
    case Miso::Chip:
      return reply;
    case Miso::FloatHigh:
      return 0xFFFFU;
    case Miso::StuckLow:
      return 0x0000U;
    case Miso::Pattern:
      return pattern;
    case Miso::EchoMosi:
      return static_cast<uint16_t>(0xFF00U | (static_cast<uint32_t>(word) >> 8U));
    case Miso::Noise:
      noise_state = (noise_state * 1103515245U) + 12345U;
      return static_cast<uint16_t>(noise_state >> 16U);
    }
    return reply;
  }

  uint8_t sampled_ = 0;           // the channel sampled at the next CS falling edge
  bool sampled_two_vref_ = false; // its range
  uint8_t sampled_next_ = 0;
  bool sampled_next_two_vref_ = false;
  uint8_t gpo_pending_ = 0;
  Program program_ = Program::None;
  uint8_t alarm_group_ = 0;
  bool restart_ = false;
  bool reset_pending_ = false;
  bool ignore_word_ = false;
  bool power_down_armed_ = false;
  int invalid_ = 0;
  bool wait_needed_ = false;
};

} // namespace ads7952_test

#include "mock_ads7952_bus.hpp"

#endif // ADS7952_MOCK_ADS7952_HPP
