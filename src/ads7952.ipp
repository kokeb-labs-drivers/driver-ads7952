/**
 * @file ads7952.ipp
 * @brief ADS7952 driver implementation, included by `ads7952.hpp`.
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * Section, table and figure numbers refer to the ADS79xx datasheet, TI SLAS605C (July 2018).
 *
 * The pipeline, as the driver models it: the reply of frame N is the conversion of the input
 * sampled at frame N's CS falling edge, which is the channel the multiplexer switched to during
 * frame N - 1, which is the one the command of frame N - 2 chose (Manual), or the next one of the
 * sequence (Auto-1, Auto-2). A range written in frame N - 2 applies to it too (Figures 50, 52, 55).
 * `stale_` counts the replies still to come that were acquired before the last change the driver
 * made (a new channel, a new sequence, a new range); a call that reads without choosing the
 * channel again first lets them pass.
 *
 * A reset behind the driver's back (a brown-out) puts every register back to its reset value:
 * Manual mode, channel 0, Range 1, no GPIO function, the reset alarm thresholds (section 8.4.3,
 * Table 11, section 8.5.3), and its first conversion is invalid (section 7.6). The chip has no flag
 * for it; what shows is a reply from channel 0 where the driver expects another channel. Every
 * reply whose address the driver can predict is checked, the ones it discards after a select
 * included, and a reply that breaks the prediction marks the driver uninitialised (fail-safe:
 * the driver never goes on writing from its copy of a configuration the chip may have lost), with
 * `DeviceReset` when the reply came from channel 0. `EnsureInitialized()` then resets and programs
 * the chip in the order of section 8.4.3 and discards the invalid first conversion.
 */
#ifndef ADS7952_IPP
#define ADS7952_IPP

namespace ads7952 {

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> ADS7952<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  if (auto r = initialize(); !r) {
    mode_ = Mode::Unknown;
    return r;
  }
  initialized_ = true;
  config_uncertain_ = false; // every register was reset and written
  detail::Log(bus_.GetPort(), LogLevel::Info, "ads7952: initialised");
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::initialize() noexcept {
  if (const Error e = Validate(config_); e != Error::Ok) {
    return fail<void>(e, "ads7952: invalid Config");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail<void>(Error::Bus, "ads7952: the port could not bring the bus up");
  }
  // Section 8.4.3: after a reset every program register and the mode control register must be
  // written for the mode of operation. The reset itself makes the start independent of whatever an
  // earlier program left behind (a GPIO2 left as the range input survives every mode word).
  if (auto r = programAll(); !r) {
    return fail<void>(r.error, "ads7952: start-up frame failed");
  }
  if (auto r = checkIdentity(); !r) {
    return fail<void>(r.error, (r.error == Error::NoDevice)
                                   ? "ads7952: every reply is 0xFFFF: nothing drives MISO"
                                   : "ads7952: the replies do not follow the channel selects");
  }
  if (config_.monitor.channel != kNoMonitor) {
    if (const auto s = checkScale(); !s) {
      return fail<void>(s.error, "ads7952: the scale monitor is out of its window");
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::programAll() noexcept {
  if (auto r = resetChip(); !r) {
    return r;
  }
  if (auto r = writeGpioProgram(config_); !r) {
    return r;
  }
  if (auto r = programAuto1(config_.auto1_channels); !r) {
    return r;
  }
  if (auto r = programAuto2(config_.auto2_last_channel); !r) {
    return r;
  }
  return writeAlarms(ResetConfig(config_.variant), config_);
}

template <SpiPort Port>
Result<void> ADS7952<Port>::writeAlarms(const Config& from, const Config& to) noexcept {
  const auto groups = static_cast<uint8_t>(static_cast<uint32_t>(ChannelCount(to.variant)) /
                                           reg::kChannelsPerAlarmGroup);
  for (uint8_t g = 0; g < groups; ++g) {
    if (auto r = writeAlarmGroup(from, to, g); !r) {
      return r;
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::resetChip() noexcept {
  // A chip left inside a program sequence by an earlier failure would take the reset word as
  // data: an Auto-1 mask, or a threshold until a word with DI12 set and one frame more. 0x1000
  // ends any of them (as a threshold it has the exit bit; as a command it is a Manual word with
  // DI11 = 0, which changes no register) and the "continue" word after it is that last frame. Found
  // by the fuzzer. Whatever they wrote, the reset clears.
  //
  // Table 11: DI09 = 1 resets every register "in the next CS frame". That frame's own command is
  // not relied on, so it is a "continue" word; it also wakes a chip left powered down (the reset
  // clears DI05), which then needs its power-up time (section 7.6).
  const std::array<uint16_t, 4> words = {
      reg::kManual, reg::kContinue,
      static_cast<uint16_t>(static_cast<uint32_t>(reg::kGpioProgram) | reg::kGpioResetAll),
      reg::kContinue};
  for (const uint16_t word : words) {
    if (auto r = frame(word); !r) {
      return Result<void>::Failure(r.error);
    }
  }
  detail::DelayUs(bus_.GetPort(), kPowerUpUs);
  mode_ = Mode::Manual; // power-on state: Manual mode, channel 0 (section 8.4.3)
  manual_channel_ = 0;
  stale_ = 2; // the first conversion after a reset is invalid (section 7.6)
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::checkIdentity() noexcept {
  // Select a, keep it one frame, select b; the replies two frames after each must carry a, a, b
  // (Figure 51). The two addresses differ in every bit the variant uses, so MISO stuck low (0x0000)
  // or high (0xFFFF), a fixed word, MOSI looped back, or a chip that never receives SDI cannot
  // pass: a deaf chip left in Manual mode repeats one address, and one left running an Auto
  // sequence never repeats one (the fuzzer found that a, b alone is what a deaf chip scanning {a,
  // b} answers). A single read of one channel is not enough: a deaf SDI can pass it.
  const detail::IdentityChannels ids = detail::IdentityFor(config_.variant);
  std::array<uint16_t, 5> replies{};
  const std::size_t count = replies.size();
  for (std::size_t i = 0; i < count; ++i) {
    const bool selects = (i == 0) || (i == 2);
    const auto r = selects ? select((i == 0) ? ids.first : ids.second) : frame(reg::kContinue);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    replies[i] = r.value;
  }
  bool all_high = true;
  for (const uint16_t reply : replies) {
    all_high = all_high && (reply == 0xFFFFU);
  }
  if (all_high) {
    return Result<void>::Failure(Error::NoDevice);
  }
  if ((reg::ReplyAddress(replies[2]) != ids.first) ||
      (reg::ReplyAddress(replies[3]) != ids.first) ||
      (reg::ReplyAddress(replies[4]) != ids.second)) {
    return Result<void>::Failure(Error::DeviceIdMismatch);
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::checkScale() noexcept {
  // The chip cannot report its range (section 8.4.2): a known input is the only witness. A select
  // carries DI06, so this proves that the chip converts in the range the driver sends (or, with
  // GPIO2 as the range input, the range the board drives) and that REFP is there.
  const uint8_t channel = config_.monitor.channel;
  if ((mode_ != Mode::Manual) || (manual_channel_ != channel)) {
    if (auto s = selectChecked(channel); !s) {
      return s;
    }
  }
  const auto r = readSelected(channel, channel);
  if (!r) {
    return r;
  }
  if (const Error e = CheckScaleCode(r.value); e != Error::Ok) {
    return Result<uint16_t>::Failure(e);
  }
  return r;
}

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::CheckScale() noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  if (config_.monitor.channel == kNoMonitor) {
    return Result<uint16_t>::Failure(Error::InvalidArgument);
  }
  auto r = checkScale();
  if ((r.error == Error::RangeMismatch) || (r.error == Error::ScaleMismatch)) {
    // Fail-safe: the scale the driver relies on is not there, so its copy of the configuration is
    // not trusted either. Range 1 where the driver sends Range 2 is the reset value of DI06
    // (section 8.4.3): a reset. With GPIO2 as the range input the board chooses the range.
    const bool reset_range = (r.error == Error::RangeMismatch) &&
                             (config_.range == Range::TwoVref) && !config_.gpio.gpio2_range_input;
    const Error e = lostTrack(r.error, reset_range ? uint8_t{0} : kNotAReset);
    return fail<uint16_t>(e, "ads7952: scale check failed");
  }
  if (!r) {
    return fail<uint16_t>(r.error, "ads7952: scale check failed");
  }
  return r;
}

template <SpiPort Port>
Result<void> ADS7952<Port>::Deinitialize() noexcept {
  if (!initialized_) {
    return Result<void>::Success();
  }
  initialized_ = false;
  // DI05 = 1: the chip powers down on the 16th SCLK falling edge of the next frame (8.3.2).
  auto r = modeFrame(
      static_cast<uint16_t>(static_cast<uint32_t>(detail::ControlBits(config_)) | reg::kPowerDown));
  if (r) {
    r = frame(reg::kContinue);
  }
  mode_ = Mode::Unknown;
  if (!r) {
    return fail<void>(r.error, "ads7952: power-down frame failed");
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::ApplyConfig(const Config& config) noexcept {
  if (const Error e = Validate(config); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (!initialized_) {
    config_ = config;
    return Result<void>::Success();
  }
  if ((config.variant != config_.variant) || (config.package != config_.package)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (auto r = writeConfig(config); !r) {
    return failedWrite(r.error, "ads7952: configuration frame failed");
  }
  config_ = config;
  config_uncertain_ = false; // every register written
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::writeConfig(const Config& to) noexcept {
  // Every register, in the order of section 8.4.3 (program registers, then the mode control
  // word), whatever the driver's copy says: no register can be read back, and a reset the replies
  // did not show (the chip resting on channel 0) leaves the copy wrong.
  if (auto r = writeGpioProgram(to); !r) {
    return r;
  }
  if (auto r = programAuto1(to.auto1_channels); !r) {
    return r;
  }
  if (auto r = programAuto2(to.auto2_last_channel); !r) {
    return r;
  }
  if (auto r = writeAlarms(detail::EveryThresholdDiffers(to), to); !r) {
    return r;
  }
  return writeControl(to);
}

template <SpiPort Port>
Result<void> ADS7952<Port>::writeGpioProgram(const Config& to) noexcept {
  auto r = frame(detail::GpioProgramWord(to));
  return r ? Result<void>::Success() : Result<void>::Failure(r.error);
}

template <SpiPort Port>
Result<void> ADS7952<Port>::writeAlarmGroup(const Config& from, const Config& to,
                                            uint8_t group) noexcept {
  // Table 13: the group frame, one frame per register (bit 2 x cc + high of `changed`), the exit
  // bit on the last one, and one frame in which the chip leaves the sequence. Only the registers
  // that change are sent.
  // `changed` collects in uint32_t (KB-CPP20-022); every shift count is below 8.
  const std::size_t first = static_cast<std::size_t>(group) * reg::kChannelsPerAlarmGroup;
  uint32_t changed = 0U;
  for (uint32_t cc = 0U; cc < reg::kChannelsPerAlarmGroup; ++cc) {
    const AlarmThreshold& a = from.alarms[first + cc];
    const AlarmThreshold& b = to.alarms[first + cc];
    const uint32_t low_bit = uint32_t{1} << (2U * cc);
    const uint32_t high_bit = uint32_t{2} << (2U * cc);
    changed |=
        ((static_cast<uint32_t>(a.low_code) >> 2U) != (static_cast<uint32_t>(b.low_code) >> 2U))
            ? low_bit
            : 0U;
    changed |=
        ((static_cast<uint32_t>(a.high_code) >> 2U) != (static_cast<uint32_t>(b.high_code) >> 2U))
            ? high_bit
            : 0U;
  }
  if (changed == 0U) {
    return Result<void>::Success();
  }
  auto r = frame(reg::AlarmGroupWord(group));
  for (uint32_t bit = 0U; bit < 8U; ++bit) {
    if (!r) {
      break;
    }
    if (((changed >> bit) & 1U) == 0U) {
      continue;
    }
    const auto cc = static_cast<uint8_t>(bit / 2U);
    const bool high = (bit & 1U) != 0U;
    const AlarmThreshold& t = to.alarms[first + cc];
    const auto value =
        static_cast<uint16_t>(static_cast<uint32_t>(high ? t.high_code : t.low_code) >> 2U);
    const bool last = (changed >> (bit + 1U)) == 0U;
    r = frame(reg::AlarmThresholdWord(cc, high, last, value));
  }
  if (r) {
    r = frame(reg::kContinue);
  }
  if (!r) {
    // A failed transfer may leave the chip inside the sequence, where it would take every later
    // word as a threshold until one has DI12 set. Finish it with channel 4g's low threshold and the
    // exit bit, then one frame: in the sequence that frame writes the intended value and exits;
    // outside it, its DI15:12 = 0001 make it a Manual word with DI11 = 0, which changes no
    // register. Found by the bus-fault sweep.
    const auto low = static_cast<uint16_t>(static_cast<uint32_t>(to.alarms[first].low_code) >> 2U);
    using detail::operator""_u8;
    (void)frame(reg::AlarmThresholdWord(0_u8, false, true, low));
    (void)frame(reg::kContinue);
    mode_ = Mode::Unknown;
    return Result<void>::Failure(r.error);
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::writeControl(const Config& to) noexcept {
  // The range and the GPIO levels; the next reply was acquired in the range the chip had before.
  auto r = modeFrame(detail::ControlBits(to));
  if (r) {
    stale_ = 1;
    r = frame(reg::kContinue); // the outputs change on this frame's CS falling edge
  }
  return r ? Result<void>::Success() : Result<void>::Failure(r.error);
}

// -------------------------------------------------------------------------------------------------
// Conversions
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::ReadChannel(uint8_t channel) noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  if (!validChannel(channel)) {
    return Result<uint16_t>::Failure(Error::InvalidArgument);
  }
  if ((mode_ != Mode::Manual) || (manual_channel_ != channel)) {
    if (auto s = selectChecked(channel); !s) {
      return fail<uint16_t>(s.error, "ads7952: channel select failed");
    }
  }
  // Opt-in reset witness: after a read of channel 0 the chip rests on `Config::park_channel`.
  const bool parks = (channel == 0U) && (config_.park_channel != kNoPark);
  auto r = readSelected(channel, parks ? config_.park_channel : channel);
  if (!r) {
    return fail<uint16_t>(r.error, "ads7952: channel read failed");
  }
  return r;
}

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::readSelected(uint8_t channel, uint8_t rest) noexcept {
  if (auto d = discardStale(); !d) {
    return Result<uint16_t>::Failure(d.error);
  }
  // The read frame is the Manual word for `channel` again, with the range and the GPIO levels
  // (DI11 = 1), not a "continue" word: the same one frame, and it puts back the channel and the
  // range a reset would have changed (section 8.4.3, Table 1). It changes nothing the chip had.
  // With `rest` other than `channel` it selects `rest` instead (the park channel).
  const auto r = frame(reg::ManualWord(rest, detail::ControlBits(config_)));
  if (!r) {
    return r;
  }
  const uint8_t address = reg::ReplyAddress(r.value);
  if (address != channel) {
    return Result<uint16_t>::Failure(lostTrack(Error::ChannelMismatch, address));
  }
  if (auto p = park(channel, rest); !p) {
    return Result<uint16_t>::Failure(p.error);
  }
  return Result<uint16_t>::Success(reg::ReplyCode(r.value));
}

template <SpiPort Port>
Result<void> ADS7952<Port>::ReadChannels(std::span<const uint8_t> channels,
                                         std::span<uint16_t> codes) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!validList(channels, codes)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  auto r = readPipelined(channels, codes);
  if (!r) {
    const auto written = codes.first(channels.size());
    std::fill(written.begin(), written.end(), uint16_t{0});
    return fail<void>(r.error, "ads7952: pipelined read failed");
  }
  return r;
}

template <SpiPort Port>
Result<void> ADS7952<Port>::readPipelined(std::span<const uint8_t> channels,
                                          std::span<uint16_t> codes) noexcept {
  // Figure 51: frame i selects channels[i]; its result comes back in frame i + 2. Every select
  // carries the range, so no reply needs to be discarded for a range change. The spans are indexed
  // below, so their sizes are checked here as well, whoever calls (KB-CPP20-043).
  const std::size_t n = channels.size();
  if ((n == 0U) || (n > kMaxChannels) || (codes.size() < n)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // The first two replies were acquired before the first select took effect: from a settled
  // Manual channel they carry that channel (a reset shows as channel 0).
  const uint8_t witness = settledChannel();
  const std::size_t frames = n + 2U;
  for (std::size_t i = 0; i < frames; ++i) {
    const auto r = (i < n) ? select(channels[i]) : frame(reg::kContinue);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    const uint8_t address = reg::ReplyAddress(r.value);
    const uint8_t expected = (i < 2U) ? witness : channels[i - 2U];
    if ((expected != kNotAReset) && (address != expected)) {
      return Result<void>::Failure(lostTrack(Error::ChannelMismatch, address));
    }
    if (i >= 2U) {
      codes[i - 2U] = reg::ReplyCode(r.value);
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<Bank> ADS7952<Port>::ReadAuto1() noexcept {
  if (!initialized_) {
    return Result<Bank>::Failure(Error::NotInitialized);
  }
  auto r = scan(Mode::Auto1, config_.auto1_channels);
  if (!r) {
    return fail<Bank>(r.error, "ads7952: Auto-1 scan failed");
  }
  return r;
}

template <SpiPort Port>
Result<Bank> ADS7952<Port>::ReadAuto2() noexcept {
  if (!initialized_) {
    return Result<Bank>::Failure(Error::NotInitialized);
  }
  auto r = scan(Mode::Auto2, detail::Auto2Mask(config_.auto2_last_channel));
  if (!r) {
    return fail<Bank>(r.error, "ads7952: Auto-2 scan failed");
  }
  return r;
}

template <SpiPort Port>
Result<Bank> ADS7952<Port>::scan(Mode mode, uint16_t mask) noexcept {
  uint8_t witness = kNotAReset;
  uint16_t entry_reply = 0U;
  if (mode_ != mode) {
    // Entering an Auto mode restarts its sequence (8.4.5, 8.4.6); DI10 says so as well. The first
    // channel is acquired in the next frame and returned in the one after. From a settled Manual
    // channel, the entry frame's reply and the one after it carry that channel.
    witness = settledChannel();
    const uint16_t word = (mode == Mode::Auto1) ? reg::kAuto1 : reg::kAuto2;
    const auto r = frame(reg::AutoWord(word, true, detail::ControlBits(config_)));
    if (!r) {
      return Result<Bank>::Failure(r.error);
    }
    entry_reply = r.value;
    mode_ = mode;
    stale_ = 1;
  }
  if (auto d = discardStale(); !d) {
    return Result<Bank>::Failure(d.error);
  }
  if (witness != kNotAReset) {
    const uint8_t a = reg::ReplyAddress(entry_reply);
    const uint8_t b = reg::ReplyAddress(last_reply_);
    if ((a != witness) || (b != witness)) {
      return Result<Bank>::Failure(lostTrack(Error::SequenceMismatch, (a != witness) ? a : b));
    }
  }
  return collect(mask);
}

template <SpiPort Port>
Result<void> ADS7952<Port>::park(uint8_t channel, uint8_t rest) noexcept {
  if (rest == channel) {
    return Result<void>::Success();
  }
  // One frame more, whose reply still comes from `channel` (acquired while the park select was
  // sent): then the chip has acquired `rest` and its next two replies carry it (Figure 51).
  const auto f = frame(reg::kContinue);
  if (!f) {
    return Result<void>::Failure(f.error);
  }
  manual_channel_ = rest;
  const uint8_t address = reg::ReplyAddress(f.value);
  if (address != channel) {
    return Result<void>::Failure(lostTrack(Error::ChannelMismatch, address));
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<Bank> ADS7952<Port>::collect(uint16_t mask) noexcept {
  // k frames return one pass: every channel of the sequence once, in ascending order with
  // wrap-round, starting wherever the chip is. Each frame is the mode word of the running Auto mode
  // with DI11 = 1 and DI10 = 0, not a "continue" word (which ignores DI11:00, Table 7): it keeps
  // the sequence counting and puts the range back each frame. After a reset (Manual mode, channel
  // 0, section 8.4.3) it enters the mode again with the reset sequence, every channel, so a pass
  // over channel 0 alone shows the reset in the next replies too.
  const uint16_t word = reg::AutoWord((mode_ == Mode::Auto2) ? reg::kAuto2 : reg::kAuto1, false,
                                      detail::ControlBits(config_));
  const uint8_t k = detail::CountChannels(mask);
  Bank bank{};
  bank.mask = mask;
  bank.range = config_.range;
  uint8_t previous = 0;
  for (uint8_t i = 0; i < k; ++i) {
    const auto r = frame(word);
    if (!r) {
      return Result<Bank>::Failure(r.error);
    }
    const uint8_t address = reg::ReplyAddress(r.value);
    const bool expected = (i == 0U) ? (((static_cast<uint32_t>(mask) >> address) & 1U) != 0U)
                                    : (address == detail::NextInSequence(mask, previous));
    if (!expected) {
      return Result<Bank>::Failure(lostTrack(Error::SequenceMismatch, address));
    }
    bank.code[address] = reg::ReplyCode(r.value);
    previous = address;
  }
  return Result<Bank>::Success(bank);
}

template <SpiPort Port>
Result<void> ADS7952<Port>::TraceFrames(uint8_t channel, std::span<uint16_t> replies) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!validChannel(channel) || replies.empty() || (replies.size() > kMaxTraceFrames)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const std::size_t count = replies.size();
  for (std::size_t i = 0; i < count; ++i) {
    const auto r = (i == 0) ? select(channel) : frame(reg::kContinue);
    if (!r) {
      std::fill(replies.begin(), replies.end(), uint16_t{0});
      return fail<void>(r.error, "ads7952: trace frame failed");
    }
    replies[i] = r.value;
  }
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Settings
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> ADS7952<Port>::SetRange(Range range) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  Config next = config_;
  next.range = range;
  if (!detail::IsEnumerator(range) || (Validate(next) != Error::Ok)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.gpio.gpio2_range_input) {
    return Result<void>::Failure(Error::Unsupported); // DI06 is ignored (Table 11, DI07)
  }
  if (auto r = modeFrame(detail::ControlBits(next)); !r) {
    return failedWrite(r.error, "ads7952: range frame failed");
  }
  config_.range = range;
  stale_ = 1; // the next reply was acquired in the old range
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::SetAuto1Channels(uint16_t mask, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!isVerify(verify) || (mask == 0U) ||
      ((static_cast<uint32_t>(mask) & ~static_cast<uint32_t>(AllChannels(config_.variant))) !=
       0U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (auto r = programAuto1(mask); !r) {
    return failedWrite(r.error, "ads7952: Auto-1 program frame failed");
  }
  config_.auto1_channels = mask;
  if (verify == Verify::ReadBack) {
    if (auto b = scan(Mode::Auto1, mask); !b) {
      const bool mismatch = (b.error == Error::SequenceMismatch) || (b.error == Error::DeviceReset);
      const Error e = mismatch ? Error::WriteMismatch : b.error;
      return failedWrite(e, "ads7952: the Auto-1 sequence read back differs");
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::SetAuto2LastChannel(uint8_t last, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!isVerify(verify) || !validChannel(last)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (auto r = programAuto2(last); !r) {
    return failedWrite(r.error, "ads7952: Auto-2 program frame failed");
  }
  config_.auto2_last_channel = last;
  if (verify == Verify::ReadBack) {
    if (auto b = scan(Mode::Auto2, detail::Auto2Mask(last)); !b) {
      const bool mismatch = (b.error == Error::SequenceMismatch) || (b.error == Error::DeviceReset);
      const Error e = mismatch ? Error::WriteMismatch : b.error;
      return failedWrite(e, "ads7952: the Auto-2 sequence read back differs");
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::programAuto1(uint16_t mask) noexcept {
  // Table 3: frame 1 opens the register, frame 2 is the mask (DI15:00, bit n = channel n).
  auto r = frame(reg::kAuto1Program);
  if (r) {
    r = frame(mask);
  }
  if (!r) {
    // A failed transfer may or may not have reached the chip; a chip still waiting for the mask
    // would take the next command as one. Send the mask once more: if the chip waits, it is the
    // mask; if not, a mask without channels 12 to 15 reads as a "continue" word (DI15:12 = 0000).
    // Found by the bus-fault sweep. A mask with channels 12 to 15 (ADS7953) cannot be repaired so.
    if ((static_cast<uint32_t>(mask) & reg::kCommandMask) == 0U) {
      (void)frame(mask);
      mode_ = Mode::Unknown;
    }
    return Result<void>::Failure(r.error);
  }
  if (mode_ == Mode::Auto1) {
    stale_ = 1; // the next reply was acquired from the old sequence
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::programAuto2(uint8_t last) noexcept {
  if (auto r = frame(reg::Auto2ProgramWord(last)); !r) {
    return Result<void>::Failure(r.error);
  }
  if (mode_ == Mode::Auto2) {
    stale_ = 1; // Auto-2 restarts at channel 0; the next reply is from the old pass
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::SetAlarmThresholds(uint8_t channel,
                                               AlarmThreshold thresholds) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!validChannel(channel) || (thresholds.low_code > kMaxCode) ||
      (thresholds.high_code > kMaxCode)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // Both registers are written whatever the driver's copy says: the copy cannot be read back.
  Config from = config_;
  from.alarms[channel] = {
      .low_code = static_cast<uint16_t>(~static_cast<uint32_t>(thresholds.low_code) & kMaxCode),
      .high_code = static_cast<uint16_t>(~static_cast<uint32_t>(thresholds.high_code) & kMaxCode)};
  Config to = config_;
  to.alarms[channel] = thresholds;
  const auto group =
      static_cast<uint8_t>(static_cast<uint32_t>(channel) / reg::kChannelsPerAlarmGroup);
  if (auto r = writeAlarmGroup(from, to, group); !r) {
    // The thresholds are never assumed set: the copy keeps the old ones and is marked uncertain.
    return failedWrite(r.error, "ads7952: alarm frame failed");
  }
  config_.alarms[channel] = thresholds;
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> ADS7952<Port>::SetGpioOutputs(uint8_t levels, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!isVerify(verify) ||
      ((static_cast<uint32_t>(levels) & ~static_cast<uint32_t>(config_.gpio.output_mask)) != 0U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  Config next = config_;
  next.gpio.output_levels = levels;
  const uint16_t ctrl = detail::ControlBits(next);
  if (auto r = modeFrame(ctrl); !r) {
    return failedWrite(r.error, "ads7952: GPIO frame failed");
  }
  config_.gpio.output_levels = levels; // the chip latched them with that frame
  if (verify == Verify::None) {
    if (auto r = frame(reg::kContinue); !r) {
      return failedWrite(r.error, "ads7952: GPIO frame failed");
    }
    return Result<void>::Success();
  }
  // The pins change on the next CS falling edge, and GPIO data are latched on a CS falling edge
  // too: ask for them in that frame (DI04 = 1), read them in the one after.
  auto r = modeFrame(static_cast<uint16_t>(static_cast<uint32_t>(ctrl) | reg::kGpioReadout));
  if (r) {
    r = modeFrame(ctrl);
  }
  if (!r) {
    return failedWrite(r.error, "ads7952: GPIO frame failed");
  }
  if ((static_cast<uint32_t>(reg::ReplyAddress(r.value)) & config_.gpio.output_mask) !=
      static_cast<uint32_t>(levels)) {
    return failedWrite(Error::WriteMismatch, "ads7952: the GPIO levels read back differ");
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<uint8_t> ADS7952<Port>::ReadGpio() noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  const uint16_t ctrl = detail::ControlBits(config_);
  auto r = modeFrame(static_cast<uint16_t>(static_cast<uint32_t>(ctrl) | reg::kGpioReadout));
  if (r) {
    r = modeFrame(ctrl);
  }
  if (!r) {
    return fail<uint8_t>(r.error, "ads7952: GPIO read failed");
  }
  const uint32_t pins = (config_.package == Package::Vqfn) ? 0x01U : 0x0FU;
  return Result<uint8_t>::Success(
      static_cast<uint8_t>(static_cast<uint32_t>(reg::ReplyAddress(r.value)) & pins));
}

// -------------------------------------------------------------------------------------------------
// Private
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::modeFrame(uint16_t control_bits) noexcept {
  // A mode word for the mode the chip is in, with DI11 = 1: Manual keeps its channel, Auto-1 and
  // Auto-2 keep counting (DI10 = 0). Neither changes what is acquired.
  switch (mode_) {
  case Mode::Auto1:
    return frame(reg::AutoWord(reg::kAuto1, false, control_bits));
  case Mode::Auto2:
    return frame(reg::AutoWord(reg::kAuto2, false, control_bits));
  case Mode::Manual:
    return frame(reg::ManualWord(manual_channel_, control_bits));
  case Mode::Unknown:
  default:
    break;
  }
  const auto r = frame(reg::ManualWord(manual_channel_, control_bits));
  if (r) {
    mode_ = Mode::Manual;
    stale_ = 1;
  }
  return r;
}

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::select(uint8_t channel) noexcept {
  const auto r = frame(reg::ManualWord(channel, detail::ControlBits(config_)));
  if (r) {
    mode_ = Mode::Manual;
    manual_channel_ = channel;
    stale_ = 1;
  }
  return r;
}

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::selectChecked(uint8_t channel) noexcept {
  // The select and the frame after it return what was acquired before the new channel took effect
  // (Figure 51): from a settled Manual channel, that channel both times. A reset in between shows
  // as channel 0 (section 8.4.3). Two frames, as before; the second is the one `stale_` skips.
  const uint8_t witness = settledChannel();
  const auto s = select(channel);
  if (!s) {
    return s;
  }
  const auto d = frame(reg::kContinue);
  if (!d) {
    return d;
  }
  if (witness != kNotAReset) {
    const uint8_t a = reg::ReplyAddress(s.value);
    const uint8_t b = reg::ReplyAddress(d.value);
    if ((a != witness) || (b != witness)) {
      return Result<uint16_t>::Failure(lostTrack(Error::ChannelMismatch, (a != witness) ? a : b));
    }
  }
  return d;
}

template <SpiPort Port>
uint8_t ADS7952<Port>::settledChannel() const noexcept {
  // The Manual channel the chip has acquired for at least two frames, so the next two replies
  // carry it; `kNotAReset` when the driver cannot predict them.
  return ((mode_ == Mode::Manual) && (stale_ == 0U)) ? manual_channel_ : kNotAReset;
}

template <SpiPort Port>
Error ADS7952<Port>::lostTrack(Error mismatch, uint8_t address) noexcept {
  // Fail-safe: the chip is not doing what the driver set up, so nothing the driver holds about it
  // (range, GPIO functions, alarm thresholds, sequences) is trusted. A reply from channel 0, the
  // power-on channel (section 8.4.3), is what a reset looks like: `DeviceReset`. Either way the
  // next `EnsureInitialized()` resets and programs the chip again.
  mode_ = Mode::Unknown;
  stale_ = 0;
  initialized_ = false;
  if (address == 0U) {
    detail::Log(bus_.GetPort(), LogLevel::Error,
                "ads7952: the chip was reset; call EnsureInitialized() to program it again");
    return Error::DeviceReset;
  }
  return mismatch;
}

template <SpiPort Port>
Result<void> ADS7952<Port>::discardStale() noexcept {
  while (stale_ > 0U) {
    if (auto r = frame(reg::kContinue); !r) {
      return Result<void>::Failure(r.error);
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<uint16_t> ADS7952<Port>::frame(uint16_t word) noexcept {
  uint16_t reply = 0xFFFFU;
  const Error e = bus_.Frame(word, reply);
  last_reply_ = reply;
  if (e != Error::Ok) {
    // The chip may or may not have taken the word: the next conversion starts from scratch.
    mode_ = Mode::Unknown;
    stale_ = 0;
    return Result<uint16_t>::Failure(e);
  }
  if (stale_ > 0U) {
    --stale_;
  }
  return Result<uint16_t>::Success(reply);
}

template <SpiPort Port>
Result<void> ADS7952<Port>::failedWrite(Error error, const char* message) noexcept {
  // A frame whose transfer failed may or may not have reached the chip: the setting it carried may
  // have taken effect. The copy is no longer the chip's state (`IsConfigUncertain()`) until
  // `ApplyConfig()` or `EnsureInitialized()` writes every register again.
  config_uncertain_ = true;
  return fail<void>(error, message);
}

template <SpiPort Port>
template <typename T>
Result<T> ADS7952<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<T>::Failure(error);
}

} // namespace ads7952

#endif // ADS7952_IPP
