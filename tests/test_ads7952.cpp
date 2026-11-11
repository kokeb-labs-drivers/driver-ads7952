/**
 * @file test_ads7952.cpp
 * @brief Driver behaviour over the chip model: lifecycle, configuration, conversions, the frame
 * pipeline, settings, GPIO and alarms.
 */
#include <array>
#include <cstdint>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"
#include "test_harness.hpp"

using ads7952::Config;
using ads7952::Error;
using ads7952::Range;
using ads7952::Verify;
using ads7952_test::FullPort;
using ads7952_test::MinimalPort;
using Model = ads7952_test::Ads7952Model;
namespace reg = ads7952::reg;

namespace {

using Adc = ads7952::ADS7952<MinimalPort>;

/// A port whose chip already runs; the driver started on it.
struct Started {
  MinimalPort port;
  Adc adc;
  explicit Started(const Config& c = {}) : adc(port, c) {}
  bool Start() {
    return adc.EnsureInitialized().ok();
  }
  [[nodiscard]] int Sent() const {
    return port.state.transfers;
  }
};

} // namespace

// --- Lifecycle
// ------------------------------------------------------------------------------------

TEST_CASE("The constructor does no I/O") {
  MinimalPort port;
  Adc adc(port);
  CHECK(!adc.IsInitialized());
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(port.state.bus_up_calls, 0);
}

TEST_CASE("Every I/O call returns NotInitialized before EnsureInitialized, and does no I/O") {
  MinimalPort port;
  Adc adc(port);
  std::array<uint8_t, 2> chans{0, 1};
  std::array<uint16_t, 2> codes{7, 7};
  std::array<uint16_t, 4> trace{};
  CHECK_EQ(adc.ReadChannel(0).error, Error::NotInitialized);
  CHECK_EQ(adc.ReadChannels(chans, codes).error, Error::NotInitialized);
  CHECK_EQ(adc.ReadAuto1().error, Error::NotInitialized);
  CHECK_EQ(adc.ReadAuto2().error, Error::NotInitialized);
  CHECK_EQ(adc.TraceFrames(0, trace).error, Error::NotInitialized);
  CHECK_EQ(adc.SetRange(Range::TwoVref).error, Error::NotInitialized);
  CHECK_EQ(adc.SetAuto1Channels(1).error, Error::NotInitialized);
  CHECK_EQ(adc.SetAuto2LastChannel(1).error, Error::NotInitialized);
  CHECK_EQ(adc.SetAlarmThresholds(0, {}).error, Error::NotInitialized);
  CHECK_EQ(adc.SetGpioOutputs(0).error, Error::NotInitialized);
  CHECK_EQ(adc.ReadGpio().error, Error::NotInitialized);
  CHECK(adc.Deinitialize().ok()); // nothing to do
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(port.state.bus_up_calls, 0); // no call starts the chip as a side effect
  CHECK_EQ(codes[0], 7U);
}

TEST_CASE("EnsureInitialized resets the chip, writes the program registers and checks identity") {
  MinimalPort port;
  port.state.chip.gpio_register = reg::kGpio2Range; // left by an earlier program: survives modes
  port.state.chip.auto1_mask = 0x0001;
  Adc adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  CHECK(adc.IsInitialized());
  CHECK_EQ(port.state.transfers, 13);
  CHECK_EQ(port.state.Word(0), reg::kManual);
  CHECK_EQ(port.state.Word(1), reg::kContinue);
  CHECK_EQ(port.state.Word(2), reg::kGpioProgram | reg::kGpioResetAll);
  CHECK_EQ(port.state.Word(3), reg::kContinue);
  CHECK_EQ(port.state.Word(4), reg::kGpioProgram);
  CHECK_EQ(port.state.Word(5), reg::kAuto1Program);
  CHECK_EQ(port.state.Word(6), 0x0FFFU);
  CHECK_EQ(port.state.Word(7), reg::Auto2ProgramWord(11));
  CHECK_EQ(port.state.Word(8), reg::ManualWord(10, 0));
  CHECK_EQ(port.state.Word(9), reg::kContinue);
  CHECK_EQ(port.state.Word(10), reg::ManualWord(5, 0));
  CHECK_EQ(port.state.chip.resets, 1);
  CHECK_EQ(port.state.chip.gpio_register, 0U);
  CHECK_EQ(port.state.chip.auto1_mask, 0x0FFFU);
  CHECK_EQ(port.state.chip.early_frames, 0); // the power-up time was waited
  CHECK_EQ(port.delayed_us, uint64_t{ads7952::kPowerUpUs});
  CHECK_EQ(port.state.chip.bad_gpio_words, 0);
  CHECK_EQ(port.state.chip.reserved_words, 0);
  CHECK_EQ(port.state.bad_transfers, 0);
}

TEST_CASE("A second EnsureInitialized does no I/O; Deinitialize then EnsureInitialized restarts") {
  Started s;
  REQUIRE(s.Start());
  const int sent = s.Sent();
  CHECK(s.adc.EnsureInitialized().ok());
  CHECK_EQ(s.Sent(), sent);
  CHECK(s.adc.Deinitialize().ok());
  CHECK(!s.adc.IsInitialized());
  CHECK_EQ(s.Sent(), sent + 2);
  CHECK(s.port.state.chip.powered_down);
  CHECK_EQ(s.adc.ReadChannel(0).error, Error::NotInitialized);
  REQUIRE(s.adc.EnsureInitialized().ok()); // wakes the chip through the reset
  CHECK(!s.port.state.chip.powered_down);
  CHECK_EQ(s.port.state.chip.early_frames, 0);
  CHECK_EQ(s.adc.ReadChannel(3).value, s.port.state.chip.CodeOf(3, false));
}

TEST_CASE("A failed start is retried from the beginning") {
  MinimalPort port;
  port.state.bus_up_ok = false;
  Adc adc(port);
  CHECK_EQ(adc.EnsureInitialized().error, Error::Bus);
  CHECK_EQ(port.state.transfers, 0);
  port.state.bus_up_ok = true;
  port.state.chip.miso = ads7952_test::Miso::StuckLow;
  CHECK_EQ(adc.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(!adc.IsInitialized());
  port.state.chip.miso = ads7952_test::Miso::Chip;
  const int before = port.state.transfers;
  CHECK(adc.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers - before, 13);
  CHECK_EQ(port.state.Word(before + 2), reg::kGpioProgram | reg::kGpioResetAll);
}

TEST_CASE("An invalid configuration is refused before any I/O") {
  Config c{};
  c.auto1_channels = 0;
  MinimalPort port;
  Adc adc(port, c);
  CHECK_EQ(adc.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(port.state.bus_up_calls, 0);
  Config q{};
  q.package = ads7952::Package::Vqfn;
  q.gpio.output_mask = 0x02;
  Adc adc2(port, q);
  CHECK_EQ(adc2.EnsureInitialized().error, Error::Unsupported);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("The identity check rejects a dead or broken bus") {
  using ads7952_test::Miso;
  const auto run = [](auto setup) {
    MinimalPort port;
    setup(port.state.chip);
    Adc adc(port);
    return adc.EnsureInitialized().error;
  };
  CHECK_EQ(run([](Model& m) { m.miso = Miso::FloatHigh; }), Error::NoDevice);
  CHECK_EQ(run([](Model& m) { m.miso = Miso::StuckLow; }), Error::DeviceIdMismatch);
  CHECK_EQ(run([](Model& m) { m.miso = Miso::Pattern; }), Error::DeviceIdMismatch);
  CHECK_EQ(run([](Model& m) {
             m.miso = Miso::Pattern;
             m.pattern = 0xA000; // address 10: a pattern that matches the first select
           }),
           Error::DeviceIdMismatch);
  CHECK_EQ(run([](Model& m) { m.miso = Miso::EchoMosi; }), Error::DeviceIdMismatch);
  CHECK_EQ(run([](Model& m) { m.deaf_sdi = true; }), Error::DeviceIdMismatch);
  // The fuzzer's find: a deaf chip left scanning Auto-1 over exactly the two identity channels.
  for (int phase = 0; phase < 2; ++phase) {
    CHECK_EQ(run([phase](Model& m) {
               (void)m.Frame(reg::kAuto1Program);
               (void)m.Frame(0x0420); // channels 5 and 10
               (void)m.Frame(reg::AutoWord(reg::kAuto1, true, 0));
               for (int i = 0; i < phase; ++i) {
                 (void)m.Frame(reg::kContinue);
               }
               m.deaf_sdi = true;
             }),
             Error::DeviceIdMismatch);
  }
  CHECK_EQ(run([](Model& m) { m.miso = Miso::Noise; }), Error::DeviceIdMismatch);
}

TEST_CASE("The identity check uses addresses that differ in every bit of each variant") {
  for (const auto v : {ads7952::Variant::Ads7950, ads7952::Variant::Ads7951,
                       ads7952::Variant::Ads7952, ads7952::Variant::Ads7953}) {
    const Config c = ads7952::ResetConfig(v);
    MinimalPort port;
    port.state.chip.variant = ads7952_test::ModelPart(v);
    Adc adc(port, c);
    REQUIRE(adc.EnsureInitialized().ok());
    const auto ids = ads7952::detail::IdentityFor(v);
    const uint32_t bits = ads7952::ChannelCount(v) - 1U;
    CHECK_EQ((static_cast<uint32_t>(ids.first) ^ static_cast<uint32_t>(ids.second)) & bits,
             bits & (bits == 15U ? 0x0FU : bits));
    CHECK(ids.first < ads7952::ChannelCount(v) && ids.second < ads7952::ChannelCount(v));
    CHECK_EQ(adc.GetChannelCount(), ads7952::ChannelCount(v));
    // A stuck MISO fails it on every variant.
    MinimalPort dead;
    dead.state.chip.variant = ads7952_test::ModelPart(v);
    dead.state.chip.miso = ads7952_test::Miso::StuckLow;
    Adc adc2(dead, c);
    CHECK_EQ(adc2.EnsureInitialized().error, Error::DeviceIdMismatch);
  }
}

TEST_CASE("A chip left inside an alarm or Auto-1 program sequence still starts cleanly") {
  // Regression for the fuzzer's first find: a failed alarm frame and a failed repair left the chip
  // taking every word as a threshold, and the next start's reset word was eaten as one.
  for (int leftover = 0; leftover < 3; ++leftover) {
    MinimalPort port;
    Model& chip = port.state.chip;
    if (leftover == 0) {
      (void)chip.Frame(reg::AlarmGroupWord(1)); // inside group 1, no exit yet
    } else if (leftover == 1) {
      (void)chip.Frame(reg::AlarmGroupWord(0));
      (void)chip.Frame(reg::AlarmThresholdWord(0, true, true, 0x100)); // exit frame pending
    } else {
      (void)chip.Frame(reg::kAuto1Program); // waiting for the mask
    }
    chip.gpio_register = reg::kGpio2Range;
    Adc adc(port);
    REQUIRE(adc.EnsureInitialized().ok());
    CHECK_EQ(chip.gpio_register, 0U); // the reset ran
    CHECK_EQ(chip.auto1_mask, 0x0FFFU);
    CHECK_EQ(chip.alarm_high[0], reg::kAlarmHighReset);
    CHECK_EQ(adc.ReadChannel(4).value, chip.CodeOf(4, false));
  }
}

TEST_CASE("Alarm groups at their reset value are not programmed at start-up") {
  Config c{};
  c.alarms[5] = {400, 3000};
  c.alarms[9].high_code = 4000;
  MinimalPort port;
  Adc adc(port, c);
  REQUIRE(adc.EnsureInitialized().ok());
  // Preamble x2, reset x2, GPIO, Auto-1 x2, Auto-2, group 1 (frame, low, high, exit), group 2
  // (frame, high, exit), identity x5.
  CHECK_EQ(port.state.transfers, 13 + 4 + 3);
  CHECK_EQ(port.state.chip.alarm_low[5], 100U);
  CHECK_EQ(port.state.chip.alarm_high[5], 750U);
  CHECK_EQ(port.state.chip.alarm_high[9], 1000U);
  CHECK_EQ(port.state.chip.alarm_low[9], 0U);
  CHECK_EQ(port.state.chip.alarm_high[0], reg::kAlarmHighReset);
  CHECK_EQ(port.state.chip.ignored_words, 2 + 1); // reset frame, one exit frame per group
}

// --- Conversions
// -----------------------------------------------------------------------------------

TEST_CASE("ReadChannel returns each channel's code, three frames for a new channel, one after") {
  Started s;
  REQUIRE(s.Start());
  for (uint8_t ch = 0; ch < 12; ++ch) {
    const int before = s.Sent();
    const auto r = s.adc.ReadChannel(ch);
    REQUIRE(r.ok());
    CHECK_EQ(r.value, s.port.state.chip.CodeOf(ch, false));
    CHECK_EQ(s.Sent() - before, 3);
    const int again = s.Sent();
    CHECK_EQ(s.adc.ReadChannel(ch).value, s.port.state.chip.CodeOf(ch, false));
    CHECK_EQ(s.Sent() - again, 1);
  }
  CHECK_EQ(s.adc.ReadChannel(12).error, Error::InvalidArgument);
}

TEST_CASE("ReadChannel refuses a reply that carries another channel's address") {
  Started s;
  REQUIRE(s.Start());
  s.port.state.lose_transfer = s.Sent(); // the select never reaches the chip
  CHECK_EQ(s.adc.ReadChannel(2).error, Error::ChannelMismatch);
  CHECK(!s.adc.IsInitialized()); // fail-safe: the chip may have lost its configuration
  CHECK_EQ(s.adc.ReadChannel(2).error, Error::NotInitialized);
  REQUIRE(s.Start());
  CHECK_EQ(s.adc.ReadChannel(2).value, s.port.state.chip.CodeOf(2, false));
}

TEST_CASE("ReadChannels pipelines the selects: list length + 2 frames") {
  Started s;
  REQUIRE(s.Start());
  const std::array<uint8_t, 5> chans{4, 7, 9, 9, 0};
  std::array<uint16_t, 5> codes{};
  const int before = s.Sent();
  REQUIRE(s.adc.ReadChannels(chans, codes).ok());
  CHECK_EQ(s.Sent() - before, 7);
  for (std::size_t i = 0; i < chans.size(); ++i) {
    CHECK_EQ(codes[i], s.port.state.chip.CodeOf(chans[i], false));
  }
  const int after = s.Sent();
  CHECK_EQ(s.adc.ReadChannel(0).value, s.port.state.chip.CodeOf(0, false));
  CHECK_EQ(s.Sent() - after, 1); // the last channel stays selected
  const std::array<uint8_t, 1> one{3};
  std::array<uint16_t, 1> code1{};
  REQUIRE(s.adc.ReadChannels(one, code1).ok());
  CHECK_EQ(code1[0], s.port.state.chip.CodeOf(3, false));
}

TEST_CASE("ReadChannels checks its arguments and every address") {
  Started s;
  REQUIRE(s.Start());
  std::array<uint8_t, 17> many{};
  std::array<uint16_t, 17> codes{};
  const std::array<uint8_t, 2> bad{1, 12};
  const int before = s.Sent();
  CHECK_EQ(s.adc.ReadChannels(std::span<const uint8_t>{}, codes).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.ReadChannels(many, codes).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.ReadChannels(bad, codes).error, Error::InvalidArgument);
  CHECK_EQ(s.adc
               .ReadChannels(std::span<const uint8_t>(many.data(), 3),
                             std::span<uint16_t>(codes.data(), 2))
               .error,
           Error::InvalidArgument);
  CHECK_EQ(s.Sent(), before);
  const std::array<uint8_t, 3> chans{1, 2, 3};
  std::array<uint16_t, 3> out{9, 9, 9};
  s.port.state.lose_transfer = s.Sent() + 1; // the select of channel 2
  CHECK_EQ(s.adc.ReadChannels(chans, out).error, Error::ChannelMismatch);
  CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0); // no partial result left behind
}

TEST_CASE("ReadAuto1 enters the sequence once (k + 2 frames), then costs k frames") {
  Config c{};
  c.auto1_channels = 0x0ACF; // eight channels, not contiguous
  Started s(c);
  REQUIRE(s.Start());
  const int k = ads7952::detail::CountChannels(0x0ACF);
  int before = s.Sent();
  auto b = s.adc.ReadAuto1();
  REQUIRE(b.ok());
  CHECK_EQ(s.Sent() - before, k + 2);
  CHECK_EQ(s.port.state.Word(before), reg::AutoWord(reg::kAuto1, true, 0));
  CHECK_EQ(b.value.mask, 0x0ACFU);
  for (uint8_t ch = 0; ch < 16; ++ch) {
    CHECK_EQ(b.value.code[ch], b.value.Has(ch) ? s.port.state.chip.CodeOf(ch, false) : 0U);
  }
  for (int pass = 0; pass < 3; ++pass) {
    before = s.Sent();
    b = s.adc.ReadAuto1();
    REQUIRE(b.ok());
    CHECK_EQ(s.Sent() - before, k);
    // Only Auto-1 words with DI11 = 1 and DI10 = 0: the sequence counts on, the range goes out
    // again.
    CHECK_EQ(s.port.state.CountCommand(2, before), k);
    CHECK_EQ(s.port.state.Word(before), reg::AutoWord(reg::kAuto1, false, 0));
    CHECK_EQ(b.value.code[11], s.port.state.chip.CodeOf(11, false));
  }
}

TEST_CASE("ReadAuto1 accepts a pass that starts anywhere in the sequence") {
  Config c{};
  c.auto1_channels = 0x0007;
  c.gpio.output_mask = 0x01;
  Started s(c);
  REQUIRE(s.Start());
  REQUIRE(s.adc.ReadAuto1().ok());
  REQUIRE(s.adc.SetGpioOutputs(1).ok()); // two frames: the sequence moved on by two
  const int before = s.Sent();
  const auto b = s.adc.ReadAuto1();
  REQUIRE(b.ok());
  CHECK_EQ(s.Sent() - before, 3);
  CHECK_EQ(b.value.code[0], s.port.state.chip.CodeOf(0, false));
  CHECK_EQ(b.value.code[2], s.port.state.chip.CodeOf(2, false));
}

TEST_CASE("ReadAuto1 detects a chip that left the sequence: DeviceReset, then a new start") {
  Started s;
  REQUIRE(s.Start());
  REQUIRE(s.adc.ReadAuto1().ok());
  s.port.state.chip.Brownout(); // back to Manual, channel 0
  CHECK_EQ(s.adc.ReadAuto1().error, Error::DeviceReset);
  CHECK_EQ(s.adc.ReadAuto1().error, Error::NotInitialized);
  REQUIRE(s.Start());
  const int before = s.Sent();
  CHECK(s.adc.ReadAuto1().ok());
  CHECK_EQ(s.Sent() - before, 12 + 2);
}

TEST_CASE("ReadAuto1 detects a lost Auto-1 mask; the new start programs it again") {
  Config c{};
  c.auto1_channels = 0x0005;
  Started s(c);
  REQUIRE(s.Start());
  REQUIRE(s.adc.ReadAuto1().ok());
  s.port.state.chip.Brownout();
  CHECK_EQ(s.adc.ReadAuto1().error, Error::DeviceReset);
  CHECK(!s.adc.IsInitialized());
  REQUIRE(s.Start());
  CHECK_EQ(s.port.state.chip.auto1_mask, 0x0005U);
  CHECK(s.adc.ReadAuto1().ok());
}

TEST_CASE("ReadAuto2 scans channel 0 to the last channel") {
  Config c{};
  c.auto2_last_channel = 3;
  Started s(c);
  REQUIRE(s.Start());
  int before = s.Sent();
  auto b = s.adc.ReadAuto2();
  REQUIRE(b.ok());
  CHECK_EQ(s.Sent() - before, 4 + 2);
  CHECK_EQ(b.value.mask, 0x000FU);
  CHECK_EQ(b.value.code[3], s.port.state.chip.CodeOf(3, false));
  before = s.Sent();
  REQUIRE(s.adc.ReadAuto2().ok());
  CHECK_EQ(s.Sent() - before, 4);
  REQUIRE(s.adc.ReadAuto1().ok()); // switching modes enters Auto-1 again
  CHECK(s.adc.ReadAuto2().ok());
}

TEST_CASE("Switching between Manual and Auto-1 re-selects and re-enters as needed") {
  Started s;
  REQUIRE(s.Start());
  REQUIRE(s.adc.ReadAuto1().ok());
  CHECK_EQ(s.adc.ReadChannel(7).value, s.port.state.chip.CodeOf(7, false));
  const int before = s.Sent();
  CHECK(s.adc.ReadAuto1().ok());
  CHECK_EQ(s.Sent() - before, 14);
}

TEST_CASE("TraceFrames returns the raw words of a manual select") {
  Started s;
  REQUIRE(s.Start());
  std::array<uint16_t, 6> words{};
  REQUIRE(s.adc.TraceFrames(11, words).ok());
  for (std::size_t i = 2; i < words.size(); ++i) {
    CHECK_EQ(reg::ReplyAddress(words[i]), 11U);
    CHECK_EQ(reg::ReplyCode(words[i]), s.port.state.chip.CodeOf(11, false));
  }
  CHECK_EQ(reg::ReplyAddress(words[1]), 5U); // the identity check's last channel, still in flight
  std::array<uint16_t, 33> too_many{};
  CHECK_EQ(s.adc.TraceFrames(11, too_many).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.TraceFrames(11, std::span<uint16_t>{}).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.TraceFrames(12, words).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.ReadChannel(11).value, s.port.state.chip.CodeOf(11, false));
}

TEST_CASE("A deaf SDI after start-up shows in the trace and fails every read") {
  Started s;
  REQUIRE(s.Start());
  s.port.state.chip.deaf_sdi = true;
  std::array<uint16_t, 6> words{};
  REQUIRE(s.adc.TraceFrames(11, words).ok());
  CHECK_EQ(reg::ReplyAddress(words[5]), 5U); // the address never follows the select
  CHECK_EQ(s.adc.ReadChannel(0).error, Error::ChannelMismatch);
  CHECK_EQ(s.adc.ReadAuto1().error, Error::NotInitialized); // fail-safe: start again
  CHECK_EQ(s.adc.EnsureInitialized().error, Error::DeviceIdMismatch);
}

// --- Range and conversion
// ----------------------------------------------------------------------------

TEST_CASE("SetRange: one frame; the next read skips the conversion made in the old range") {
  Config c{};
  c.va_min_mv = 5000;
  Started s(c);
  REQUIRE(s.Start());
  REQUIRE(s.adc.ReadChannel(4).ok());
  int before = s.Sent();
  REQUIRE(s.adc.SetRange(Range::TwoVref).ok());
  CHECK_EQ(s.Sent() - before, 1);
  CHECK_EQ(s.adc.GetConfig().range, Range::TwoVref);
  before = s.Sent();
  CHECK_EQ(s.adc.ReadChannel(4).value, s.port.state.chip.CodeOf(4, true));
  CHECK_EQ(s.Sent() - before, 2);
  CHECK_EQ(s.adc.ReadChannel(6).value, s.port.state.chip.CodeOf(6, true));
  REQUIRE(s.adc.ReadAuto1().ok());
  REQUIRE(s.adc.SetRange(Range::Vref).ok());
  before = s.Sent();
  const auto b = s.adc.ReadAuto1();
  REQUIRE(b.ok());
  CHECK_EQ(s.Sent() - before, 12 + 1);
  for (uint8_t ch = 0; ch < 12; ++ch) {
    CHECK_EQ(b.value.code[ch], s.port.state.chip.CodeOf(ch, false));
  }
}

TEST_CASE("SetRange refuses what the board or the datasheet does not allow") {
  Config c{};
  c.va_min_mv = 4900; // 2 x 2.5 V above +VA
  Started s(c);
  REQUIRE(s.Start());
  const int before = s.Sent();
  CHECK_EQ(s.adc.SetRange(Range::TwoVref).error, Error::InvalidArgument);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(s.adc.SetRange(static_cast<Range>(2)).error, Error::InvalidArgument);
  CHECK_EQ(s.Sent(), before);
  Config g{};
  g.gpio.gpio2_range_input = true;
  Started t(g);
  REQUIRE(t.Start());
  CHECK_EQ(t.adc.SetRange(Range::Vref).error, Error::Unsupported);
}

TEST_CASE("GPIO2 as the range input: the pin, not DI06, selects the range") {
  Config g{};
  g.gpio.gpio2_range_input = true;
  Started s(g);
  s.port.state.chip.gpio_inputs = 0x04;
  REQUIRE(s.Start());
  CHECK_EQ(s.adc.ReadChannel(3).value, s.port.state.chip.CodeOf(3, true));
}

TEST_CASE("ToMicrovolts: one LSB is full scale / 4096 (Table 8)") {
  Config c{};
  Started s(c);
  CHECK_EQ(s.adc.ToMicrovolts(0), 0U);
  CHECK_EQ(s.adc.ToMicrovolts(2048), 1250000U);
  CHECK_EQ(s.adc.ToMicrovolts(4095), 2499389U);
  CHECK_EQ(ads7952::CodeToMicrovolts(2048, Range::TwoVref, 2500), 2500000U);
  CHECK_EQ(ads7952::CodeToMicrovolts(0xFFFF, Range::Vref, 2500), 2499389U); // 12 bits only
}

TEST_CASE("ToMicrovolts of a Bank uses the range the bank was converted in") {
  Config c{};
  c.range = Range::TwoVref;
  Started s(c);
  REQUIRE(s.Start());
  const auto b = s.adc.ReadAuto1();
  REQUIRE(b.ok());
  CHECK_EQ(b.value.range, Range::TwoVref);
  REQUIRE(s.adc.SetRange(Range::Vref).ok());
  CHECK_EQ(s.adc.ToMicrovolts(b.value, 4),
           ads7952::CodeToMicrovolts(b.value.code[4], Range::TwoVref, 2500));
  CHECK(s.adc.ToMicrovolts(b.value, 4) != s.adc.ToMicrovolts(b.value.code[4]));
  CHECK_EQ(s.adc.ToMicrovolts(b.value, 15), 0U); // not in the bank
}

// --- Scale monitor
// -------------------------------------------------------------------------------------

namespace {

/// Range 2, 2.5 V reference, REFP itself on channel 11.
Config MonitoredConfig() {
  Config c{};
  c.range = Range::TwoVref;
  c.monitor = {.channel = 11, .expected_uv = 2500000U, .tolerance_uv = 100000U};
  return c;
}

} // namespace

TEST_CASE("The scale monitor is checked at start-up: three frames more") {
  Started plain(Config{.range = Range::TwoVref});
  REQUIRE(plain.Start());
  Started s(MonitoredConfig());
  s.port.state.chip.input_mv[11] = 2500;
  REQUIRE(s.Start());
  CHECK_EQ(s.Sent() - plain.Sent(), 3);
  CHECK_EQ(ads7952::CheckScaleCode(MonitoredConfig(), 2048), Error::Ok);
}

TEST_CASE("The start-up scale check tells a lost range from a missing reference") {
  Started r1(MonitoredConfig());
  r1.port.state.chip.input_mv[11] = 1250; // what Range 1 makes of 2.5 V is 4095; this is half
  CHECK_EQ(r1.adc.EnsureInitialized().error, Error::ScaleMismatch);
  Config g = MonitoredConfig();
  g.gpio.gpio2_range_input = true;
  Started low(g);
  low.port.state.chip.input_mv[11] = 2500;
  low.port.state.chip.gpio_inputs = 0x00; // GPIO2 low: Range 1
  CHECK_EQ(low.adc.EnsureInitialized().error, Error::RangeMismatch);
  CHECK(!low.adc.IsInitialized());
  low.port.state.chip.gpio_inputs = 0x04; // strap fixed
  CHECK(low.adc.EnsureInitialized().ok());
}

TEST_CASE("A range lost during an Auto-1 run comes back at the next frame, at no frame cost") {
  Started s(MonitoredConfig());
  s.port.state.chip.input_mv[11] = 2500;
  REQUIRE(s.Start());
  auto b = s.adc.ReadAuto1();
  REQUIRE(b.ok());
  CHECK_EQ(s.adc.CheckScaleCode(b.value.code[11]), Error::Ok);
  s.port.state.chip.two_vref = false; // a lost mode word left the chip in Range 1
  const int before = s.Sent();
  (void)s.adc.ReadAuto1(); // its first two replies were acquired in Range 1
  b = s.adc.ReadAuto1();   // every frame of a pass carries DI06 again
  REQUIRE(b.ok());
  CHECK_EQ(s.Sent() - before, 2 * 12);
  CHECK(s.port.state.chip.two_vref);
  for (uint8_t ch = 0; ch < 12; ++ch) {
    CHECK_EQ(b.value.code[ch], s.port.state.chip.CodeOf(ch, true));
  }
  CHECK_EQ(s.adc.CheckScaleCode(b.value.code[11]), Error::Ok);
}

TEST_CASE("CheckScale costs a ReadChannel of the monitor and refuses without one") {
  Started s(MonitoredConfig());
  s.port.state.chip.input_mv[11] = 2500;
  CHECK_EQ(s.adc.CheckScale().error, Error::NotInitialized);
  CHECK_EQ(s.Sent(), 0);
  REQUIRE(s.Start());
  int before = s.Sent();
  REQUIRE(s.adc.CheckScale().ok()); // the start-up check left channel 11 selected
  CHECK_EQ(s.Sent() - before, 1);
  REQUIRE(s.adc.ReadChannel(3).ok());
  before = s.Sent();
  const auto r = s.adc.CheckScale();
  REQUIRE(r.ok());
  CHECK_EQ(r.value, 2048);
  CHECK_EQ(s.Sent() - before, 3);
  s.port.state.chip.input_mv[11] = 0;
  CHECK_EQ(s.adc.CheckScale().error, Error::ScaleMismatch);
  Started none;
  REQUIRE(none.Start());
  before = none.Sent();
  CHECK_EQ(none.adc.CheckScale().error, Error::InvalidArgument);
  CHECK_EQ(none.adc.CheckScaleCode(2048), Error::InvalidArgument);
  CHECK_EQ(none.Sent(), before);
}

TEST_CASE("Validate refuses a scale monitor that could not tell the ranges apart") {
  const auto check = [](auto edit, Error expected) {
    Config c = MonitoredConfig();
    edit(c);
    CHECK_EQ(ads7952::Validate(c), expected);
  };
  check([](Config&) {}, Error::Ok);
  check([](Config& c) { c.monitor = {}; }, Error::Ok);
  check([](Config& c) { c.monitor.channel = 12; }, Error::InvalidArgument); // ADS7952 has 0-11
  check([](Config& c) { c.monitor.tolerance_uv = 0; }, Error::InvalidArgument);
  check([](Config& c) { c.monitor.expected_uv = 0; }, Error::InvalidArgument);
  check([](Config& c) { c.monitor.expected_uv = 4990000U; }, Error::InvalidArgument);  // clips
  check([](Config& c) { c.monitor.tolerance_uv = 1300000U; }, Error::InvalidArgument); // overlap
  // REFP itself is full scale in Range 1: no window there. REFP / 2 works in both ranges.
  check([](Config& c) { c.range = Range::Vref; }, Error::InvalidArgument);
  check(
      [](Config& c) {
        c.range = Range::Vref;
        c.monitor.expected_uv = 1250000U;
      },
      Error::Ok);
  Started s(MonitoredConfig());
  s.port.state.chip.input_mv[11] = 2500;
  REQUIRE(s.Start());
  const int before = s.Sent();
  CHECK_EQ(s.adc.SetRange(Range::Vref).error, Error::InvalidArgument);
  CHECK_EQ(s.Sent(), before);
}

// --- Program registers
// ---------------------------------------------------------------------------------

TEST_CASE("SetAuto1Channels: two frames; Verify::ReadBack scans the new sequence") {
  Started s;
  REQUIRE(s.Start());
  int before = s.Sent();
  REQUIRE(s.adc.SetAuto1Channels(0x0003).ok());
  CHECK_EQ(s.Sent() - before, 2);
  CHECK_EQ(s.port.state.chip.auto1_mask, 0x0003U);
  CHECK_EQ(s.adc.GetConfig().auto1_channels, 0x0003U);
  before = s.Sent();
  REQUIRE(s.adc.SetAuto1Channels(0x0030, Verify::ReadBack).ok());
  CHECK_EQ(s.Sent() - before, 2 + 2 + 2);
  before = s.Sent();
  REQUIRE(s.adc.ReadAuto1().ok());
  CHECK_EQ(s.Sent() - before, 2); // the read-back left the sequence running
  // A new mask while in Auto-1: one reply from the old sequence is skipped.
  REQUIRE(s.adc.SetAuto1Channels(0x0700).ok());
  before = s.Sent();
  const auto b = s.adc.ReadAuto1();
  REQUIRE(b.ok());
  CHECK_EQ(s.Sent() - before, 3 + 1);
  CHECK_EQ(b.value.code[9], s.port.state.chip.CodeOf(9, false));
}

TEST_CASE("SetAuto1Channels checks its arguments; ReadBack catches a lost mask frame") {
  Started s;
  REQUIRE(s.Start());
  const int before = s.Sent();
  CHECK_EQ(s.adc.SetAuto1Channels(0).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.SetAuto1Channels(0x1000).error, Error::InvalidArgument);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(s.adc.SetAuto1Channels(1, static_cast<Verify>(2)).error, Error::InvalidArgument);
  CHECK_EQ(s.Sent(), before);
  s.port.state.lose_transfer = s.Sent() + 1;
  CHECK(s.adc.SetAuto1Channels(0x0006).ok()); // without ReadBack the loss is not seen
  CHECK_EQ(s.adc.ReadAuto1().error, Error::SequenceMismatch);
  REQUIRE(s.Start()); // the mismatch left the driver uninitialised
  s.port.state.lose_transfer = s.Sent() + 1;
  CHECK_EQ(s.adc.SetAuto1Channels(0x0006, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK(!s.adc.IsInitialized());
}

TEST_CASE("SetAuto2LastChannel: one frame; ReadBack scans; Auto-2 restarts at channel 0") {
  Started s;
  REQUIRE(s.Start());
  int before = s.Sent();
  REQUIRE(s.adc.SetAuto2LastChannel(2).ok());
  CHECK_EQ(s.Sent() - before, 1);
  CHECK_EQ(s.port.state.chip.auto2_last, 2U);
  REQUIRE(s.adc.ReadAuto2().ok());
  REQUIRE(s.adc.SetAuto2LastChannel(4, Verify::ReadBack).ok());
  before = s.Sent();
  REQUIRE(s.adc.ReadAuto2().ok());
  CHECK_EQ(s.Sent() - before, 5);
  CHECK_EQ(s.adc.SetAuto2LastChannel(12).error, Error::InvalidArgument);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(s.adc.SetAuto2LastChannel(1, static_cast<Verify>(9)).error, Error::InvalidArgument);
  s.port.state.lose_transfer = s.Sent();
  CHECK_EQ(s.adc.SetAuto2LastChannel(7, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK(!s.adc.IsInitialized());
}

TEST_CASE("SetAlarmThresholds writes both registers of the channel: four frames") {
  Started s;
  REQUIRE(s.Start());
  const int before = s.Sent();
  REQUIRE(s.adc.SetAlarmThresholds(6, {1000, 3000}).ok());
  CHECK_EQ(s.Sent() - before, 4);
  CHECK_EQ(s.port.state.Word(before), reg::AlarmGroupWord(1));
  CHECK_EQ(s.port.state.chip.alarm_low[6], 250U);
  CHECK_EQ(s.port.state.chip.alarm_high[6], 750U);
  CHECK_EQ(s.adc.GetConfig().alarms[6].high_code, 3000U);
  REQUIRE(s.adc.SetAlarmThresholds(6, {1000, 3000}).ok()); // the same again: still written
  CHECK_EQ(s.Sent() - before, 8);
  CHECK_EQ(s.adc.SetAlarmThresholds(12, {}).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.SetAlarmThresholds(0, {4096, 4095}).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.SetAlarmThresholds(0, {0, 4096}).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.ReadChannel(6).value, s.port.state.chip.CodeOf(6, false)); // modes unaffected
}

TEST_CASE("Alarm outputs follow the thresholds on GPIO0 and GPIO1") {
  Config c{};
  c.gpio.alarm_output = ads7952::AlarmOutput::Gpio1LowGpio0High;
  c.alarms[2] = {0, 400}; // channel 2 reads 400 mV = 655 codes: above 400
  c.alarms[3] = {4000, 4095};
  Started s(c);
  REQUIRE(s.Start());
  REQUIRE(s.adc.ReadChannel(2).ok());
  CHECK(s.port.state.chip.alarm_high_out);
  REQUIRE(s.adc.ReadChannel(3).ok());
  CHECK(s.port.state.chip.alarm_low_out);
  const auto pins = s.adc.ReadGpio();
  REQUIRE(pins.ok());
  CHECK_EQ(pins.value & 0x02U, 0x02U); // GPIO1: low alarm
}

// --- GPIO
// ---------------------------------------------------------------------------------------------

TEST_CASE("SetGpioOutputs: two frames, the pins change before it returns") {
  Config c{};
  c.gpio.output_mask = 0x0C;
  c.gpio.output_levels = 0x04;
  Started s(c);
  REQUIRE(s.Start());
  CHECK_EQ(s.port.state.chip.gpo & 0x0C, 0x04); // the start-up wrote the configured levels
  const int before = s.Sent();
  REQUIRE(s.adc.SetGpioOutputs(0x08).ok());
  CHECK_EQ(s.Sent() - before, 2);
  CHECK_EQ(s.port.state.chip.PinLevels() & 0x0C, 0x08);
  CHECK_EQ(s.adc.GetConfig().gpio.output_levels, 0x08U);
  CHECK_EQ(s.adc.SetGpioOutputs(0x01).error, Error::InvalidArgument); // GPIO0 is an input
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(s.adc.SetGpioOutputs(0x04, static_cast<Verify>(3)).error, Error::InvalidArgument);
  CHECK_EQ(s.adc.ReadChannel(1).value, s.port.state.chip.CodeOf(1, false)); // addresses again
}

TEST_CASE("SetGpioOutputs with Verify::ReadBack: three frames; catches a pin the board holds") {
  Config c{};
  c.gpio.output_mask = 0x03;
  Started s(c);
  REQUIRE(s.Start());
  int before = s.Sent();
  REQUIRE(s.adc.SetGpioOutputs(0x03, Verify::ReadBack).ok());
  CHECK_EQ(s.Sent() - before, 3);
  s.port.state.chip.gpio_held = 0x02;     // GPIO1 shorted low on the board
  CHECK(s.adc.SetGpioOutputs(0x02).ok()); // without ReadBack: not seen
  CHECK_EQ(s.adc.SetGpioOutputs(0x02, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(s.adc.GetConfig().gpio.output_levels, 0x02U);
  REQUIRE(s.adc.ReadAuto1().ok()); // the reply format is back to addresses
  s.port.state.chip.gpio_held = 0;
  CHECK(s.adc.SetGpioOutputs(0x01, Verify::ReadBack).ok()); // in Auto-1 too
  CHECK(s.adc.ReadAuto1().ok());
}

TEST_CASE("ReadGpio returns the pin levels: two frames; GPIO0 only on VQFN") {
  Config c{};
  Started s(c);
  s.port.state.chip.gpio_inputs = 0x0A;
  REQUIRE(s.Start());
  const int before = s.Sent();
  const auto r = s.adc.ReadGpio();
  REQUIRE(r.ok());
  CHECK_EQ(r.value, 0x0AU);
  CHECK_EQ(s.Sent() - before, 2);
  CHECK_EQ(s.adc.ReadChannel(5).value, s.port.state.chip.CodeOf(5, false));
  Config q{};
  q.package = ads7952::Package::Vqfn;
  q.gpio.output_mask = 0x01;
  Started t(q);
  t.port.state.chip.package = ads7952_test::chip::Package::Vqfn;
  t.port.state.chip.gpio_inputs = 0x0E;
  REQUIRE(t.Start());
  CHECK_EQ(t.port.state.chip.gpio_register & 0x0FU, 0x0FU); // GPIO1 to 3 programmed as outputs
  REQUIRE(t.adc.SetGpioOutputs(0x01, Verify::ReadBack).ok());
  CHECK_EQ(t.adc.ReadGpio().value, 0x01U);
}

TEST_CASE("Power-down input on GPIO3 and the GPIO program register") {
  Config c{};
  c.gpio.gpio3_power_down_input = true;
  c.gpio.alarm_output = ads7952::AlarmOutput::Gpio0HighOrLow;
  c.gpio.output_mask = 0x06;
  Started s(c);
  s.port.state.chip.gpio_inputs = 0x08; // PD high: running
  REQUIRE(s.Start());
  CHECK_EQ(s.port.state.chip.gpio_register,
           reg::kGpio3PowerDown | (1U << reg::kGpioAlarmShift) | 0x06U);
  CHECK(s.adc.ReadChannel(1).ok());
}

// --- ApplyConfig
// ----------------------------------------------------------------------------------------

TEST_CASE("ApplyConfig before the start only stores, with no I/O") {
  MinimalPort port;
  Adc adc(port);
  Config c{};
  c.auto1_channels = 0x0011;
  CHECK(adc.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(adc.GetConfig().auto1_channels, 0x0011U);
  Config bad{};
  bad.auto2_last_channel = 12;
  CHECK_EQ(adc.ApplyConfig(bad).error, Error::InvalidArgument);
  CHECK_EQ(adc.GetConfig().auto1_channels, 0x0011U);
  REQUIRE(adc.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.auto1_mask, 0x0011U);
}

TEST_CASE("ApplyConfig on a running driver writes every register, changed or not") {
  // GPIO 1, Auto-1 2, Auto-2 1, three alarm groups of 10 (frame, 8 registers, exit), mode word 1,
  // GPO flush 1: 36 frames on the ADS7952 whatever changed (section 8.4.3).
  constexpr int kFullWrite = 1 + 2 + 1 + (3 * 10) + 2;
  Started s;
  REQUIRE(s.Start());
  int before = s.Sent();
  CHECK(s.adc.ApplyConfig(Config{}).ok());
  CHECK_EQ(s.Sent() - before, kFullWrite);
  Config c{};
  c.gpio.output_mask = 0x01;
  c.gpio.output_levels = 0x01;
  c.auto1_channels = 0x0F0;
  c.auto2_last_channel = 4;
  c.alarms[11] = {100, 4095};
  c.range = Range::TwoVref;
  before = s.Sent();
  REQUIRE(s.adc.ApplyConfig(c).ok());
  CHECK_EQ(s.Sent() - before, kFullWrite);
  CHECK_EQ(s.port.state.chip.auto1_mask, 0x00F0U);
  CHECK_EQ(s.port.state.chip.auto2_last, 4U);
  CHECK_EQ(s.port.state.chip.alarm_low[11], 25U);
  CHECK_EQ(s.port.state.chip.PinLevels() & 1U, 1U);
  CHECK(s.port.state.chip.two_vref);
  CHECK_EQ(s.adc.ReadChannel(5).value, s.port.state.chip.CodeOf(5, true));
  Config other = c;
  other.variant = ads7952::Variant::Ads7953;
  before = s.Sent();
  CHECK_EQ(s.adc.ApplyConfig(other).error, Error::InvalidArgument);
  other = c;
  other.package = ads7952::Package::Vqfn;
  CHECK_EQ(s.adc.ApplyConfig(other).error, Error::InvalidArgument);
  CHECK_EQ(s.Sent(), before);
  Config range_only = c;
  range_only.range = Range::Vref;
  REQUIRE(s.adc.ApplyConfig(range_only).ok());
  CHECK_EQ(s.Sent() - before, kFullWrite);
  CHECK_EQ(s.adc.ReadChannel(5).value, s.port.state.chip.CodeOf(5, false));
}

// --- Validate and helpers
// --------------------------------------------------------------------------------

TEST_CASE("Config{} is the reset state and valid; Validate rejects each bad field") {
  constexpr Config kD{};
  static_assert(ads7952::Validate(kD) == Error::Ok);
  const Config reset = ads7952::ResetConfig(ads7952::Variant::Ads7952);
  CHECK_EQ(reset.auto1_channels, kD.auto1_channels);
  CHECK_EQ(reset.auto2_last_channel, kD.auto2_last_channel);
  const auto check = [](auto edit, Error expected) {
    Config c{};
    edit(c);
    CHECK_EQ(ads7952::Validate(c), expected);
  };
  // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange): out-of-range enums test Validate()
  check([](Config& c) { c.variant = static_cast<ads7952::Variant>(4); }, Error::InvalidArgument);
  check([](Config& c) { c.package = static_cast<ads7952::Package>(2); }, Error::InvalidArgument);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  check([](Config& c) { c.range = static_cast<Range>(2); }, Error::InvalidArgument);
  check([](Config& c) { c.gpio.alarm_output = static_cast<ads7952::AlarmOutput>(3); },
        Error::InvalidArgument);
  check([](Config& c) { c.gpio.alarm_output = static_cast<ads7952::AlarmOutput>(5); },
        Error::InvalidArgument);
  check([](Config& c) { c.gpio.alarm_output = static_cast<ads7952::AlarmOutput>(7); },
        Error::InvalidArgument);
  // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
  check([](Config& c) { c.vref_mv = 1999; }, Error::InvalidArgument);
  check([](Config& c) { c.vref_mv = 3001; }, Error::InvalidArgument);
  check([](Config& c) { c.va_min_mv = 2699; }, Error::InvalidArgument);
  check([](Config& c) { c.va_min_mv = 5251; }, Error::InvalidArgument);
  check([](Config& c) { c.range = Range::TwoVref; }, Error::Ok);
  check(
      [](Config& c) {
        c.range = Range::TwoVref;
        c.va_min_mv = 4999;
      },
      Error::InvalidArgument);
  check([](Config& c) { c.auto1_channels = 0; }, Error::InvalidArgument);
  check([](Config& c) { c.auto1_channels = 0x1000; }, Error::InvalidArgument);
  check([](Config& c) { c.auto2_last_channel = 12; }, Error::InvalidArgument);
  check([](Config& c) { c.alarms[0].low_code = 4096; }, Error::InvalidArgument);
  check([](Config& c) { c.alarms[0].high_code = 4096; }, Error::InvalidArgument);
  check([](Config& c) { c.alarms[12].high_code = 100; }, Error::InvalidArgument);
  check([](Config& c) { c.alarms[12].low_code = 3; }, Error::Ok); // still the reset register
  check([](Config& c) { c.alarms[13].low_code = 4; }, Error::InvalidArgument);
  check([](Config& c) { c.gpio.output_mask = 0x10; }, Error::InvalidArgument);
  check([](Config& c) { c.gpio.output_levels = 0x01; }, Error::InvalidArgument);
  check(
      [](Config& c) {
        c.gpio.alarm_output = ads7952::AlarmOutput::Gpio0High;
        c.gpio.output_mask = 0x01;
      },
      Error::InvalidArgument);
  check(
      [](Config& c) {
        c.gpio.alarm_output = ads7952::AlarmOutput::Gpio1Low;
        c.gpio.output_mask = 0x02;
      },
      Error::InvalidArgument);
  check(
      [](Config& c) {
        c.gpio.gpio2_range_input = true;
        c.gpio.output_mask = 0x04;
      },
      Error::InvalidArgument);
  check(
      [](Config& c) {
        c.gpio.gpio3_power_down_input = true;
        c.gpio.output_mask = 0x08;
      },
      Error::InvalidArgument);
  const auto qfn = [](auto edit, Error expected) {
    Config c{};
    c.package = ads7952::Package::Vqfn;
    edit(c);
    CHECK_EQ(ads7952::Validate(c), expected);
  };
  qfn([](Config& c) { c.gpio.output_mask = 0x01; }, Error::Ok);
  qfn([](Config& c) { c.gpio.alarm_output = ads7952::AlarmOutput::Gpio0HighOrLow; }, Error::Ok);
  qfn([](Config& c) { c.gpio.alarm_output = ads7952::AlarmOutput::Gpio1Low; }, Error::Unsupported);
  qfn([](Config& c) { c.gpio.gpio2_range_input = true; }, Error::Unsupported);
  qfn([](Config& c) { c.gpio.gpio3_power_down_input = true; }, Error::Unsupported);
  qfn([](Config& c) { c.gpio.output_mask = 0x08; }, Error::Unsupported);
}

TEST_CASE("Variant helpers, reply decoding and frame builders") {
  CHECK_EQ(ads7952::ChannelCount(ads7952::Variant::Ads7950), 4U);
  CHECK_EQ(ads7952::ChannelCount(ads7952::Variant::Ads7953), 16U);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(ads7952::ChannelCount(static_cast<ads7952::Variant>(9)), 0U);
  CHECK_EQ(ads7952::AllChannels(ads7952::Variant::Ads7951), 0x00FFU);
  CHECK_EQ(ads7952::AllChannels(ads7952::Variant::Ads7953), 0xFFFFU);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(ads7952::AllChannels(static_cast<ads7952::Variant>(9)), 0U);
  CHECK_EQ(ads7952::ResetConfig(ads7952::Variant::Ads7950).auto2_last_channel, 3U);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK_EQ(ads7952::ResetConfig(static_cast<ads7952::Variant>(9)).auto2_last_channel, 0U);
  CHECK_EQ(reg::ReplyAddress(0xB123), 11U);
  CHECK_EQ(reg::ReplyCode(0xB123), 0x123U);
  CHECK_EQ(reg::ManualWord(4, 0x40), 0x1A40U); // DI15:12 0001, DI11, channel 4, Range 2
  CHECK_EQ(reg::AutoWord(reg::kAuto1, true, 0), 0x2C00U);
  CHECK_EQ(reg::Auto2ProgramWord(11), 0x92C0U);
  CHECK_EQ(reg::AlarmGroupWord(2), 0xE000U);
  CHECK_EQ(reg::AlarmThresholdWord(3, true, true, 0x3FF), 0xF3FFU);
  CHECK_EQ(ads7952::detail::NextInSequence(0x0ACF, 11), 0U);
  CHECK_EQ(ads7952::detail::NextInSequence(0x0ACF, 3), 6U);
  volatile uint16_t empty = 0; // a run-time call, not folded at compile time
  CHECK_EQ(ads7952::detail::NextInSequence(empty, 3), 3U);
  CHECK_EQ(ads7952::detail::CountChannels(0x0ACF), 8U);
  CHECK_EQ(ads7952::detail::Auto2Mask(15), 0xFFFFU);
  ads7952::Bank b{};
  b.mask = 0x0003;
  CHECK(b.Has(1) && !b.Has(2) && !b.Has(16));
}

TEST_CASE("ToString names every error") {
  for (int e = 0; e < 32; ++e) {
    const char* name = ads7952::ToString(static_cast<Error>(e));
    REQUIRE(name != nullptr);
  }
  CHECK(ads7952::ToString(Error::SequenceMismatch)[0] == 'S');
  CHECK(ads7952::ToString(Error::RangeMismatch)[0] == 'R');
  CHECK(ads7952::ToString(Error::ScaleMismatch)[1] == 'c');
  CHECK(ads7952::ToString(static_cast<Error>(10))[0] == 'U');
  CHECK(ads7952::ToString(static_cast<Error>(200))[0] == 'U');
}

// --- Port hooks
// --------------------------------------------------------------------------------------------

TEST_CASE("The Log hook gets one line per lifecycle event and failure") {
  FullPort port;
  ads7952::ADS7952<FullPort> adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  CHECK_EQ(port.log_lines, 1);
  CHECK_EQ(port.error_lines, 0);
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(adc.ReadChannel(0).error, Error::Bus);
  CHECK_EQ(port.error_lines, 1);
  CHECK(port.delayed_us >= 1U);
  CHECK_EQ(adc.GetLastReply(), 0xFFFFU);
  FullPort dead;
  dead.state.chip.miso = ads7952_test::Miso::FloatHigh;
  ads7952::ADS7952<FullPort> adc2(dead);
  CHECK_EQ(adc2.EnsureInitialized().error, Error::NoDevice);
  CHECK_EQ(dead.error_lines, 1);
  CHECK_EQ(adc2.GetLastReply(), 0xFFFFU);
}

TEST_CASE("Every transfer is one 16-bit frame with both buffers") {
  Started s;
  REQUIRE(s.Start());
  std::array<uint16_t, 4> words{};
  CHECK(s.adc.ReadAuto1().ok());
  CHECK(s.adc.ReadChannel(1).ok());
  CHECK(s.adc.TraceFrames(2, words).ok());
  CHECK(s.adc.ReadGpio().ok());
  CHECK(s.adc.Deinitialize().ok());
  CHECK_EQ(s.port.state.bad_transfers, 0);
  CHECK_EQ(s.port.state.chip.reserved_words, 0);
  CHECK_EQ(s.port.state.chip.bad_gpio_words, 0);
}

TEST_CASE("Settings after a failed frame start from a Manual word; in Auto-2 they keep counting") {
  Config c{};
  c.gpio.output_mask = 0x01;
  c.auto2_last_channel = 2;
  Started s(c);
  REQUIRE(s.Start());
  s.port.state.fail_transfer = s.Sent(); // the driver no longer knows what the chip converts
  CHECK_EQ(s.adc.ReadChannel(1).error, Error::Bus);
  int before = s.Sent();
  REQUIRE(s.adc.SetRange(Range::TwoVref).ok());
  CHECK_EQ(s.port.state.Word(before) >> 12U, 1); // a Manual word
  CHECK_EQ(s.adc.ReadChannel(5).value, s.port.state.chip.CodeOf(5, true));
  REQUIRE(s.adc.ReadAuto2().ok());
  before = s.Sent();
  REQUIRE(s.adc.SetGpioOutputs(1).ok());
  CHECK_EQ(s.port.state.Word(before), reg::AutoWord(reg::kAuto2, false, reg::kRangeTwoVref | 1U));
  const int after = s.Sent();
  REQUIRE(s.adc.ReadAuto2().ok()); // still running: one frame per channel
  CHECK_EQ(s.Sent() - after, 3);
}
