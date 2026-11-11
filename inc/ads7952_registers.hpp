/**
 * @file ads7952_registers.hpp
 * @brief ADS795x command and reply words: the bit fields of every frame (datasheet SLAS605C,
 * section 8.4 and 8.5, Tables 1 to 13).
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The chip has no addressable registers. Every frame is one 16-bit word on SDI, MSB first, with
 * CS low for exactly 16 SCLK cycles; its top four bits (DI15:12) say what the word is: a mode
 * control word (Manual, Auto-1, Auto-2 or "continue") or the first or only frame of a program
 * register (GPIO, Auto-1 channel mask, Auto-2 last channel, alarm thresholds). At the same time
 * the chip shifts out one 16-bit reply on SDO: a 4-bit channel address (or the GPIO levels) in
 * DO15:12 and the 12-bit conversion result in DO11:00.
 *
 * Everything here is `constexpr` masks and shifts; there are no bit-field unions, whose layout
 * depends on the compiler.
 */
#ifndef ADS7952_REGISTERS_HPP
#define ADS7952_REGISTERS_HPP

#include <cstdint>

#include "ads7952_detail.hpp"

/** @brief The ADS795x frame format. */
namespace ads7952::reg {

/**
 * @defgroup ads7952_registers Frame format
 * @brief Command words, program registers and the reply word.
 * @{
 */

// --- DI15:12, what the word is (Tables 1, 2, 3, 5, 6, 7, 11, 13) --------------------------------
inline constexpr uint32_t kCommandShift = 12;      ///< DI15:12 hold the command.
inline constexpr uint16_t kCommandMask = 0xF000U;  ///< DI15:12.
inline constexpr uint16_t kContinue = 0x0000U;     ///< 0000: keep the mode, ignore DI11:00.
inline constexpr uint16_t kManual = 0x1000U;       ///< 0001: Manual mode (Table 1).
inline constexpr uint16_t kAuto1 = 0x2000U;        ///< 0010: Auto-1 mode (Table 2).
inline constexpr uint16_t kAuto2 = 0x3000U;        ///< 0011: Auto-2 mode (Table 5).
inline constexpr uint16_t kGpioProgram = 0x4000U;  ///< 0100: GPIO program register (Table 11).
inline constexpr uint16_t kAuto1Program = 0x8000U; ///< 1000: Auto-1 mask in the next frame.
inline constexpr uint16_t kAuto2Program = 0x9000U; ///< 1001: Auto-2 last channel (Table 6).
inline constexpr uint16_t kAlarmProgramGroup0 = 0xC000U; ///< 11bb: alarm group bb (Table 13).

// --- Mode control word, DI11:00 (Tables 1, 2, 5) -------------------------------------------------
inline constexpr uint16_t kProgramEnable = 0x0800U; ///< DI11: take DI10:00 (Manual: DI06:00).
inline constexpr uint16_t kAutoReset = 0x0400U;     ///< DI10, Auto modes: restart the sequence.
inline constexpr uint32_t kManualChannelShift = 7;  ///< DI10:07, Manual: the next channel.
inline constexpr uint16_t kRangeTwoVref = 0x0040U;  ///< DI06: 0 to 2 x VREF (Range 2).
inline constexpr uint16_t kPowerDown = 0x0020U;     ///< DI05: power down at the next frame's end.
inline constexpr uint16_t kGpioReadout = 0x0010U;  ///< DI04: next reply carries GPIO3:0 in DO15:12.
inline constexpr uint16_t kGpioDataMask = 0x000FU; ///< DI03:00: GPIO output levels.
inline constexpr uint16_t kControlMask = 0x007FU;  ///< DI06:00: the bits a mode word programs.

// --- Auto-2 program register (Table 6) -----------------------------------------------------------
inline constexpr uint32_t kAuto2LastShift = 6; ///< DI09:06: the last channel of the sequence.

// --- GPIO program register (Table 11) ------------------------------------------------------------
inline constexpr uint16_t kGpioResetAll = 0x0200U;      ///< DI09: reset every register next frame.
inline constexpr uint16_t kGpio3PowerDown = 0x0100U;    ///< DI08: GPIO3 is the power-down input.
inline constexpr uint16_t kGpio2Range = 0x0080U;        ///< DI07: GPIO2 is the range input.
inline constexpr uint32_t kGpioAlarmShift = 4;          ///< DI06:04: GPIO0/GPIO1 alarm outputs.
inline constexpr uint16_t kGpioDirectionMask = 0x000FU; ///< DI03:00: 1 = output.

// --- Alarm program register, frame 2 onwards (Table 13) ------------------------------------------
inline constexpr uint32_t kAlarmChannelShift = 14; ///< DI15:14: channel within the group.
inline constexpr uint16_t kAlarmHigh = 0x2000U;    ///< DI13: 1 high threshold, 0 low.
inline constexpr uint16_t kAlarmExit = 0x1000U;    ///< DI12: leave the sequence after this frame.
inline constexpr uint16_t kAlarmThresholdMask = 0x03FFU; ///< DI09:00: the upper 10 result bits.
inline constexpr uint16_t kAlarmHighReset = 0x03FFU;     ///< High threshold after reset: all ones.
inline constexpr uint16_t kAlarmLowReset = 0x0000U;      ///< Low threshold after reset: all zeros.
inline constexpr uint32_t kChannelsPerAlarmGroup = 4;    ///< Table 12: four channels per group.

// --- Reply word (section 8.1, Figure 2) ----------------------------------------------------------
inline constexpr uint32_t kReplyAddressShift = 12; ///< DO15:12: channel address or GPIO3:0.
inline constexpr uint16_t kReplyCodeMask =
    0x0FFFU; ///< DO11:00: the 12-bit result, straight binary.

// --- Frame builders ------------------------------------------------------------------------------
// Every operand is widened to uint32_t before a shift or a bitwise operator, and every shift count
// is a constant below 16 (handbook KB-CPP20-020, -021; MISRA C++:2023 Rules 7.0.4, 7.0.5).

/**
 * @brief The 4-bit field DO15:12 of a reply: the channel the result belongs to (DI04 = 0 in the
 * frame before), or the levels of GPIO3:0 (DI04 = 1).
 * @param reply The 16-bit reply word.
 * @return 0 to 15.
 */
[[nodiscard]] constexpr uint8_t ReplyAddress(uint16_t reply) noexcept {
  return static_cast<uint8_t>((static_cast<uint32_t>(reply) >> kReplyAddressShift) & 0x0FU);
}

/**
 * @brief The 12-bit conversion result of a reply, DO11:00.
 * @param reply The 16-bit reply word.
 * @return 0 to 4095.
 */
[[nodiscard]] constexpr uint16_t ReplyCode(uint16_t reply) noexcept {
  return static_cast<uint16_t>(static_cast<uint32_t>(reply) & kReplyCodeMask);
}

/**
 * @brief A Manual mode word with DI11 = 1: select `channel` for the next frame and set DI06:00.
 * @param channel 0 to 15.
 * @param control DI06:00 (range, power-down, GPIO readout, GPIO levels).
 * @return The word.
 */
[[nodiscard]] constexpr uint16_t ManualWord(uint8_t channel, uint16_t control) noexcept {
  return static_cast<uint16_t>(static_cast<uint32_t>(kManual) | kProgramEnable |
                               ((static_cast<uint32_t>(channel) & 0x0FU) << kManualChannelShift) |
                               (static_cast<uint32_t>(control) & kControlMask));
}

/**
 * @brief An Auto-1 or Auto-2 mode word with DI11 = 1.
 * @param mode `kAuto1` or `kAuto2`.
 * @param restart Set DI10: start the sequence again at its first channel.
 * @param control DI06:00.
 * @return The word.
 */
[[nodiscard]] constexpr uint16_t AutoWord(uint16_t mode, bool restart, uint16_t control) noexcept {
  return static_cast<uint16_t>((static_cast<uint32_t>(mode) & kCommandMask) | kProgramEnable |
                               (restart ? static_cast<uint32_t>(kAutoReset) : 0U) |
                               (static_cast<uint32_t>(control) & kControlMask));
}

/**
 * @brief The Auto-2 program word: `last` becomes the last channel of the Auto-2 sequence.
 * @param last 0 to 15.
 * @return The word.
 */
[[nodiscard]] constexpr uint16_t Auto2ProgramWord(uint8_t last) noexcept {
  return static_cast<uint16_t>(static_cast<uint32_t>(kAuto2Program) |
                               ((static_cast<uint32_t>(last) & 0x0FU) << kAuto2LastShift));
}

/**
 * @brief The first frame of an alarm group's programming sequence.
 * @param group 0 to 3 (channels 4 x group to 4 x group + 3).
 * @return The word.
 */
[[nodiscard]] constexpr uint16_t AlarmGroupWord(uint8_t group) noexcept {
  return static_cast<uint16_t>(static_cast<uint32_t>(kAlarmProgramGroup0) |
                               ((static_cast<uint32_t>(group) & 0x03U) << kCommandShift));
}

/**
 * @brief One alarm threshold frame of a group's programming sequence.
 * @param channel_in_group 0 to 3.
 * @param high The high threshold (else the low one).
 * @param exit Leave the sequence after this frame.
 * @param threshold The 10-bit register value: the upper 10 bits of a 12-bit result.
 * @return The word.
 */
[[nodiscard]] constexpr uint16_t AlarmThresholdWord(uint8_t channel_in_group, bool high, bool exit,
                                                    uint16_t threshold) noexcept {
  return static_cast<uint16_t>(
      ((static_cast<uint32_t>(channel_in_group) & 0x03U) << kAlarmChannelShift) |
      (high ? static_cast<uint32_t>(kAlarmHigh) : 0U) |
      (exit ? static_cast<uint32_t>(kAlarmExit) : 0U) |
      (static_cast<uint32_t>(threshold) & kAlarmThresholdMask));
}

// The builders against the bit tables of the datasheet (KB-CPP20-012): a field in the wrong place
// fails the build. Each argument has the parameter's type (MISRA C++:2023 Rule 7.0.6).
using detail::operator""_u8;
using detail::operator""_u16;
// Table 1: Manual, DI11, channel 3 in DI10:07.
static_assert(ManualWord(3_u8, 0_u16) == 0x1980U);
// Table 1: channel 4, Range 2 (DI06); the channel and control fields are masked to their width.
static_assert((ManualWord(4_u8, kRangeTwoVref) == 0x1A40U) &&
              (ManualWord(0x14_u8, 0xFFFF_u16) == 0x1A7FU));
// Tables 2 and 5: Auto-1 with DI10 (restart); Auto-2 keeps counting with DI06 and GPIO0 high.
static_assert((AutoWord(kAuto1, true, 0_u16) == 0x2C00U) &&
              (AutoWord(kAuto2, false, 0x41_u16) == 0x3841U));
// Table 6: last channel 11 in DI09:06.
static_assert(Auto2ProgramWord(11_u8) == 0x92C0U);
// Table 13: group 2 is 1110; channel 3 of a group, high threshold, exit, all ones.
static_assert((AlarmGroupWord(2_u8) == 0xE000U) && (AlarmGroupWord(3_u8) == 0xF000U));
static_assert(AlarmThresholdWord(3_u8, true, true, 0x3FF_u16) == 0xF3FFU);
static_assert(AlarmThresholdWord(0_u8, false, false, 0xFFFF_u16) == 0x03FFU);
// Section 8.1: DO15:12 the address, DO11:00 the code.
static_assert((ReplyAddress(0xB123_u16) == 11U) && (ReplyCode(0xB123_u16) == 0x123U));

/** @} */

} // namespace ads7952::reg

#endif // ADS7952_REGISTERS_HPP
