/**
 * @file test_reset.cpp
 * @brief A reset the chip does on its own (a brown-out) in the middle of a session: what the model
 * does (sections 7.6, 8.4.3, 8.5.3, Table 11), how the driver detects it, and that nothing it sends
 * afterwards leaves the chip in its reset state while the driver reports the old configuration.
 */
#include <array>
#include <cstdint>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"
#include "test_harness.hpp"

using ads7952::AlarmOutput;
using ads7952::Config;
using ads7952::Error;
using ads7952::Range;
using ads7952_test::Ads7952Model;
using ads7952_test::MinimalPort;
using Pipeline = ads7952_test::Ads7952Model::Pipeline;
namespace reg = ads7952::reg;

namespace {

using Adc = ads7952::ADS7952<MinimalPort>;

/// Range 2, GPIO0 the "high or low" alarm output, thresholds on channels 0 and 3: the set-up of an
/// interlock that a reset silently disarms.
Config InterlockConfig() {
  Config c{};
  c.range = Range::TwoVref;
  c.gpio.alarm_output = AlarmOutput::Gpio0HighOrLow;
  c.alarms[0] = {.low_code = 400, .high_code = 1000};
  c.alarms[3] = {.low_code = 400, .high_code = 1000};
  return c;
}

/// The chip holds `c` (the registers the driver writes, compared with the model's own).
bool ChipHolds(const Ads7952Model& m, const Config& c) {
  bool ok = (m.gpio_register == (ads7952::detail::GpioProgramWord(c) & 0x01FFU)) &&
            (m.two_vref == (c.range == Range::TwoVref)) && (m.auto1_mask == c.auto1_channels) &&
            (m.auto2_last == c.auto2_last_channel);
  for (std::size_t ch = 0; ch < 12U; ++ch) {
    ok = ok && (m.alarm_low[ch] == (c.alarms[ch].low_code >> 2U)) &&
         (m.alarm_high[ch] == (c.alarms[ch].high_code >> 2U));
  }
  return ok;
}

constexpr uint16_t kInvalidCode = 0x0A5A; // what the model returns for an invalid conversion

} // namespace

TEST_CASE("Model: a brown-out mid-session reloads the reset values and gives one invalid result") {
  Ads7952Model m;
  m.two_vref = true;
  m.gpio_register = 0x0011;
  m.alarm_high[3] = 100;
  m.auto1_mask = 0x0003;
  (void)m.Frame(reg::ManualWord(3, reg::kRangeTwoVref));
  (void)m.Frame(reg::kContinue);
  (void)m.Frame(reg::kContinue);
  m.brownout_before_frame = m.frames; // on its own, before the next frame
  const int resets = m.resets;
  const uint16_t first = m.Frame(reg::kContinue);
  CHECK_EQ(m.resets, resets + 1);
  CHECK_EQ(reg::ReplyAddress(first), 3U); // Pipeline::Kept: the conversion in flight survives
  CHECK_EQ(reg::ReplyCode(first), kInvalidCode);
  CHECK(!m.two_vref);
  CHECK_EQ(m.gpio_register, 0U);
  CHECK_EQ(m.alarm_high[3], 0x03FFU);
  CHECK_EQ(m.alarm_low[3], 0U);
  CHECK_EQ(m.auto1_mask, 0x0FFFU);
  const uint16_t second = m.Frame(reg::kContinue);
  CHECK_EQ(reg::ReplyAddress(second), 0U); // Manual mode, channel 0 (section 8.4.3)
  CHECK_EQ(reg::ReplyCode(second), m.CodeOf(0, false));

  Ads7952Model lost;
  (void)lost.Frame(reg::ManualWord(3, 0));
  (void)lost.Frame(reg::kContinue);
  lost.brownout_before_frame = lost.frames;
  lost.brownout_pipeline = Pipeline::Lost;
  const uint16_t gone = lost.Frame(reg::kContinue);
  CHECK_EQ(reg::ReplyAddress(gone), 0U);
  CHECK_EQ(reg::ReplyCode(gone), kInvalidCode);
  CHECK_EQ(lost.Frame(reg::kContinue), lost.CodeOf(0, false)); // address 0, valid from now on
}

TEST_CASE("Audit scenario: brown-out, ReadChannel, ApplyConfig(GetConfig()), the GPIO0 alarm") {
  MinimalPort p;
  Adc d(p, InterlockConfig());
  REQUIRE(d.EnsureInitialized().ok());
  Ads7952Model& m = p.state.chip;
  REQUIRE(d.ReadChannel(0).ok());
  REQUIRE(ChipHolds(m, d.GetConfig()));
  m.Brownout();
  // Channel 0 is the channel the chip powers up on, so no reply address shows this reset. The
  // first result after it is the invalid conversion (section 7.6), the second one was acquired in
  // Range 1 before the read frame put Range 2 back (docs/limitations.md).
  (void)d.ReadChannel(0);
  CHECK(m.two_vref); // every read frame is the Manual word with the range again
  (void)d.ReadChannel(0);
  const auto again = d.ReadChannel(0);
  REQUIRE(again.ok());
  CHECK_EQ(again.value, m.CodeOf(0, true));
  auto r3 = d.ReadChannel(3);
  REQUIRE(r3.ok());
  CHECK_EQ(r3.value, m.CodeOf(3, true));
  // ApplyConfig() never trusts its copy: the same configuration is written in full.
  const int before = m.frames;
  REQUIRE(d.ApplyConfig(d.GetConfig()).ok());
  CHECK_EQ(m.frames - before, 1 + 2 + 1 + (3 * 10) + 2);
  CHECK(ChipHolds(m, d.GetConfig()));
  m.input_mv[3] = 2400; // above the high threshold (1000 codes ~ 1.22 V in Range 2)
  r3 = d.ReadChannel(3);
  REQUIRE(r3.ok());
  REQUIRE(d.ReadChannel(3).ok());
  CHECK_EQ(m.PinLevels() & 0x01U, 1U); // the interlock trips again
}

TEST_CASE("A reset between two reads of the same channel is DeviceReset; the start reprograms") {
  MinimalPort p;
  Adc d(p, InterlockConfig());
  REQUIRE(d.EnsureInitialized().ok());
  Ads7952Model& m = p.state.chip;
  REQUIRE(d.ReadChannel(3).ok());
  REQUIRE(d.ReadChannel(3).ok());
  m.brownout_before_frame = m.frames; // the next frame the driver sends
  // The conversion in flight survives (Pipeline::Kept) and carries channel 3's address: the one
  // reset that no reply shows. The next one comes from channel 0.
  (void)d.ReadChannel(3);
  CHECK_EQ(d.ReadChannel(3).error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
  int before = m.frames;
  CHECK_EQ(d.ReadChannel(3).error, Error::NotInitialized);
  CHECK_EQ(d.ApplyConfig(d.GetConfig()).error, Error::Ok); // stores only
  CHECK_EQ(m.frames, before);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.ReadChannel(3).ok());
  CHECK(ChipHolds(m, d.GetConfig()));
  before = m.frames;
  REQUIRE(d.ReadChannel(3).ok());
  CHECK_EQ(m.frames - before, 1); // the frame count of a read is unchanged
}

TEST_CASE("A reset seen in the replies a new select discards is DeviceReset") {
  MinimalPort p;
  Adc d(p, InterlockConfig());
  REQUIRE(d.EnsureInitialized().ok());
  Ads7952Model& m = p.state.chip;
  REQUIRE(d.ReadChannel(7).ok());
  m.Brownout();
  // The two replies in flight while channel 3 is selected must come from channel 7.
  CHECK_EQ(d.ReadChannel(3).error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
  // The same for a pipelined list and an Auto-1 entry from a settled Manual channel.
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.ReadChannel(7).ok());
  m.Brownout();
  const std::array<uint8_t, 2> list{1, 2};
  std::array<uint16_t, 2> codes{};
  CHECK_EQ(d.ReadChannels(list, codes).error, Error::DeviceReset);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.ReadChannel(7).ok());
  m.Brownout();
  CHECK_EQ(d.ReadAuto1().error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
}

TEST_CASE("A reset that also loses the conversion in flight is DeviceReset at once") {
  MinimalPort p;
  Adc d(p, InterlockConfig());
  REQUIRE(d.EnsureInitialized().ok());
  Ads7952Model& m = p.state.chip;
  REQUIRE(d.ReadChannel(3).ok());
  m.brownout_before_frame = m.frames;
  m.brownout_pipeline = Pipeline::Lost;
  CHECK_EQ(d.ReadChannel(3).error, Error::DeviceReset); // the invalid result is not returned
  CHECK(!d.IsInitialized());
}

TEST_CASE("A reset during Auto-1 scans is DeviceReset; ReadChannels after one is consistent") {
  MinimalPort p;
  Config c = InterlockConfig();
  c.auto1_channels = 0x0F0E; // channel 0 not in the sequence
  Adc d(p, c);
  REQUIRE(d.EnsureInitialized().ok());
  Ads7952Model& m = p.state.chip;
  REQUIRE(d.ReadAuto1().ok());
  m.brownout_before_frame = m.frames + 2;
  CHECK_EQ(d.ReadAuto1().error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
  CHECK_EQ(d.ReadAuto1().error, Error::NotInitialized);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(ChipHolds(m, d.GetConfig()));
  REQUIRE(d.ReadAuto1().ok());
}

TEST_CASE("A lost frame (another channel's address) still fails safe: the driver must restart") {
  MinimalPort p;
  Adc d(p);
  REQUIRE(d.EnsureInitialized().ok());
  p.state.lose_transfer = p.state.transfers; // the select never reaches the chip
  CHECK_EQ(d.ReadChannel(2).error, Error::ChannelMismatch);
  CHECK(!d.IsInitialized());
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(d.ReadChannel(2).value, p.state.chip.CodeOf(2, false));
}

TEST_CASE("CheckScale after a reset fails safe: DeviceReset or a scale error, then uninitialised") {
  Config c{};
  c.range = Range::TwoVref;
  c.monitor = {.channel = 11, .expected_uv = 2500000U, .tolerance_uv = 100000U};
  for (const Pipeline pipeline : {Pipeline::Kept, Pipeline::Lost}) {
    MinimalPort p;
    p.state.chip.input_mv[11] = 2500;
    Adc d(p, c);
    REQUIRE(d.EnsureInitialized().ok());
    REQUIRE(d.CheckScale().ok());
    p.state.chip.brownout_before_frame = p.state.chip.frames;
    p.state.chip.brownout_pipeline = pipeline;
    const auto r = d.CheckScale();
    CHECK(!r.ok());
    if (pipeline == Pipeline::Lost) {
      CHECK_EQ(r.error, Error::DeviceReset);
    }
    CHECK(!d.IsInitialized());
  }
}

TEST_CASE("CheckScale: Range 1 where the driver sent Range 2 is DeviceReset") {
  Config c{};
  c.range = Range::TwoVref;
  c.monitor = {.channel = 11, .expected_uv = 1250000U, .tolerance_uv = 100000U};
  MinimalPort p;
  p.state.chip.input_mv[11] = 1250;
  Adc d(p, c);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.CheckScale().ok());
  // Stage the reset's range alone (DI06 back to 0, section 8.4.3) with no invalid conversion, as
  // two frames would leave it if the read frame did not send the range again.
  p.state.chip.two_vref = false;
  p.state.chip.acquiring_two_vref = false;
  (void)p.state.chip.Frame(reg::kContinue);
  (void)p.state.chip.Frame(reg::kContinue);
  CHECK_EQ(d.CheckScale().error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
}

TEST_CASE("ApplyConfig writes every register, whatever the driver's copy says") {
  for (uint8_t v = 0; v < 4U; ++v) {
    Config c{};
    c.variant = static_cast<ads7952::Variant>(v);
    c.auto1_channels = 0x0003;
    c.auto2_last_channel = 1;
    c.gpio.alarm_output = AlarmOutput::Gpio0High;
    c.alarms[1] = {.low_code = 800, .high_code = 2000};
    MinimalPort p;
    p.state.chip.variant = static_cast<ads7952_test::chip::Part>(v);
    Adc d(p, c);
    REQUIRE(d.EnsureInitialized().ok());
    p.state.chip.Brownout(); // undetected so far: the driver's copy is stale
    const int before = p.state.chip.frames;
    REQUIRE(d.ApplyConfig(c).ok());
    const int groups = ads7952::ChannelCount(c.variant) / 4;
    CHECK_EQ(p.state.chip.frames - before, 1 + 2 + 1 + (10 * groups) + 2);
    CHECK_EQ(p.state.chip.gpio_register, ads7952::detail::GpioProgramWord(c) & 0x01FFU);
    CHECK_EQ(p.state.chip.auto1_mask, 0x0003U);
    CHECK_EQ(p.state.chip.auto2_last, 1U);
    CHECK_EQ(p.state.chip.alarm_low[1], 200U);
    CHECK_EQ(p.state.chip.alarm_high[1], 500U);
    CHECK_EQ(p.state.chip.alarm_high[0], 0x03FFU);
  }
}

TEST_CASE("A write whose transfer failed may have taken effect: the copy is marked uncertain") {
  MinimalPort p;
  Adc d(p, InterlockConfig());
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.IsConfigUncertain());
  Ads7952Model& m = p.state.chip;
  // The alarm frame with channel 6's high threshold reaches the chip, but the port reports a
  // failure: the chip holds the new threshold, the driver keeps the old one and never assumes the
  // new one is set.
  p.state.fail_transfer = p.state.transfers + 2;
  p.state.fail_after_clocking = true;
  CHECK_EQ(d.SetAlarmThresholds(6, {.low_code = 1000, .high_code = 3000}).error, Error::Bus);
  CHECK(d.IsConfigUncertain());
  CHECK_EQ(d.GetConfig().alarms[6].high_code, 4095U); // not the value of the failed write
  CHECK_EQ(m.alarm_high[6], 750U);                    // ... which the chip took all the same
  // ApplyConfig() writes every register, with no difference taken from the copy.
  const int before = m.frames;
  REQUIRE(d.ApplyConfig(d.GetConfig()).ok());
  CHECK_EQ(m.frames - before, 1 + 2 + 1 + (3 * 10) + 2);
  CHECK(!d.IsConfigUncertain());
  CHECK(ChipHolds(m, d.GetConfig()));

  // The same for the range, a sequence, the GPIO levels and ApplyConfig() itself.
  const auto failing = [&](auto call) {
    p.state.fail_transfer = p.state.transfers;
    p.state.fail_after_clocking = true;
    CHECK_EQ(call().error, Error::Bus);
    CHECK(d.IsConfigUncertain());
    if (!d.IsInitialized()) {
      REQUIRE(d.EnsureInitialized().ok());
    }
    REQUIRE(d.ApplyConfig(d.GetConfig()).ok());
    CHECK(!d.IsConfigUncertain());
    CHECK(ChipHolds(m, d.GetConfig()));
  };
  failing([&] { return d.SetRange(Range::Vref); });
  failing([&] { return d.SetAuto1Channels(0x0003); });
  failing([&] { return d.SetAuto2LastChannel(2); });
  failing([&] { return d.SetGpioOutputs(0); });
  Config other = InterlockConfig();
  other.alarms[9] = {.low_code = 100, .high_code = 200};
  failing([&] { return d.ApplyConfig(other); });
  CHECK_EQ(d.GetConfig().alarms[9].high_code, 4095U); // the failed ApplyConfig() stored nothing
  // A new start clears it as well.
  failing([&] { return d.SetRange(Range::Vref); });
  p.state.fail_transfer = p.state.transfers;
  CHECK_EQ(d.SetRange(Range::Vref).error, Error::Bus);
  REQUIRE(d.Deinitialize().ok());
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.IsConfigUncertain());
}

TEST_CASE(
    "ReadChannel after SetRange: a failed skipped frame is Bus, the next read selects again") {
  MinimalPort p;
  Adc d(p);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.ReadChannel(4).ok());
  REQUIRE(d.SetRange(Range::TwoVref).ok());
  p.state.fail_transfer = p.state.transfers; // the frame that skips the old-range reply
  CHECK_EQ(d.ReadChannel(4).error, Error::Bus);
  CHECK(d.IsInitialized());
  CHECK_EQ(d.ReadChannel(4).value, p.state.chip.CodeOf(4, true));
}

TEST_CASE("A reset during an Auto-1 sequence of channel 0 alone is detected") {
  for (const Pipeline pipeline : {Pipeline::Kept, Pipeline::Lost}) {
    MinimalPort p;
    Config c = InterlockConfig();
    c.auto1_channels = 0x0001;
    Adc d(p, c);
    REQUIRE(d.EnsureInitialized().ok());
    Ads7952Model& m = p.state.chip;
    REQUIRE(d.ReadAuto1().ok());
    REQUIRE(d.ReadAuto1().ok());
    m.Brownout(pipeline); // the replies of channel 0 alone look the same before and after
    Error e = Error::Ok;
    for (int i = 0; (i < 4) && (e == Error::Ok); ++i) {
      e = d.ReadAuto1().error;
    }
    CHECK(e == Error::SequenceMismatch || e == Error::DeviceReset);
    CHECK(!d.IsInitialized());
    CHECK(m.two_vref); // every frame of the pass sent the range again
    REQUIRE(d.EnsureInitialized().ok());
    CHECK(ChipHolds(m, d.GetConfig()));
  }
}

TEST_CASE("A reset during an Auto-2 sequence that ends at channel 0 is detected") {
  for (const Pipeline pipeline : {Pipeline::Kept, Pipeline::Lost}) {
    MinimalPort p;
    Config c = InterlockConfig();
    c.auto2_last_channel = 0;
    Adc d(p, c);
    REQUIRE(d.EnsureInitialized().ok());
    Ads7952Model& m = p.state.chip;
    REQUIRE(d.ReadAuto2().ok());
    REQUIRE(d.ReadAuto2().ok());
    m.Brownout(pipeline);
    Error e = Error::Ok;
    for (int i = 0; (i < 4) && (e == Error::Ok); ++i) {
      e = d.ReadAuto2().error;
    }
    CHECK(e == Error::SequenceMismatch || e == Error::DeviceReset);
    CHECK(!d.IsInitialized());
    CHECK(m.two_vref);
  }
}

TEST_CASE("Config::park_channel makes a reset during channel-0 reads visible") {
  Config c = InterlockConfig();
  c.park_channel = 7;
  CHECK_EQ(ads7952::Validate(c), Error::Ok);
  Config bad = c;
  bad.park_channel = 0;
  CHECK_EQ(ads7952::Validate(bad), Error::InvalidArgument);
  bad.park_channel = 12; // the ADS7952 has channels 0 to 11
  CHECK_EQ(ads7952::Validate(bad), Error::InvalidArgument);

  MinimalPort p;
  Adc d(p, c);
  REQUIRE(d.EnsureInitialized().ok());
  Ads7952Model& m = p.state.chip;
  auto r = d.ReadChannel(0);
  REQUIRE(r.ok());
  CHECK_EQ(r.value, m.CodeOf(0, true));
  int before = m.frames;
  r = d.ReadChannel(0); // select 0, the frame in flight, the read with the park select, one more
  REQUIRE(r.ok());
  CHECK_EQ(r.value, m.CodeOf(0, true));
  CHECK_EQ(m.frames - before, 4);
  CHECK_EQ(m.manual_next, 7U); // the chip rests on the park channel
  before = m.frames;
  REQUIRE(d.ReadChannel(7).ok()); // already there: one frame
  CHECK_EQ(m.frames - before, 1);
  REQUIRE(d.ReadChannel(0).ok());
  // The reset that probe 6 of the review could not see: in a loop of channel-0 reads, any moment.
  for (const Pipeline pipeline : {Pipeline::Kept, Pipeline::Lost}) {
    REQUIRE(d.EnsureInitialized().ok());
    REQUIRE(d.ReadChannel(0).ok());
    REQUIRE(d.ReadChannel(0).ok());
    m.Brownout(pipeline);
    Error e = Error::Ok;
    for (int i = 0; (i < 3) && (e == Error::Ok); ++i) {
      e = d.ReadChannel(0).error;
    }
    CHECK_EQ(e, Error::DeviceReset);
    CHECK(!d.IsInitialized());
  }
  // A failed park frame is Bus; the next read starts from an unknown mode.
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.ReadChannel(0).ok());
  p.state.fail_transfer = p.state.transfers + 3;
  CHECK_EQ(d.ReadChannel(0).error, Error::Bus);
  CHECK_EQ(d.ReadChannel(0).value, m.CodeOf(0, true));
  // A reply in the park frame from another channel is a mismatch as well.
  REQUIRE(d.ReadChannel(0).ok());
  p.state.replace_transfer = p.state.transfers + 3;
  p.state.replace_word = 0x5123;
  CHECK_EQ(d.ReadChannel(0).error, Error::ChannelMismatch);
  CHECK(!d.IsInitialized());
}
