/**
 * @file test_bus_faults.cpp
 * @brief Every public operation reports a failed transfer as Error::Bus, whichever one fails, and
 * the next call starts the pipeline again from a select or an entry.
 *
 * Each operation is run once on a clean bus to count its frames, then twice per frame with exactly
 * that one failing: once before the frame reaches the chip and once after the chip took it (a port
 * can report an error for a frame that went out). This reaches every "the transfer failed, return
 * the error" path in the driver, which a single fault test per operation would not. The setters are
 * swept with `Verify::None` and again with `Verify::ReadBack`, so the read-back frames are swept
 * too.
 */
#include <array>
#include <cstdint>
#include <cstdio>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"
#include "test_harness.hpp"

using ads7952::Config;
using ads7952::Error;
using ads7952::Verify;
using ads7952_test::MinimalPort;

namespace {

using Adc = ads7952::ADS7952<MinimalPort>;

Config SweepConfig() {
  Config c{};
  c.gpio.output_mask = 0x03;
  c.auto1_channels = 0x0F0F;
  c.auto2_last_channel = 5;
  // Channel 11 of the model sits at 1.75 V: a scale monitor, so the start-up check and
  // CheckScale() are swept too.
  c.monitor = {.channel = 11, .expected_uv = 1750000U, .tolerance_uv = 100000U};
  return c;
}

/// Runs `prepare` then `op` on a started driver, with each of op's frames failing in turn; every
/// run must return Error::Bus, and a ReadChannel() afterwards must succeed.
template <typename Prepare, typename Op>
void SweepBusFaults(const char* name, Prepare prepare, Op op) {
  int frames = 0;
  {
    MinimalPort clean;
    Adc adc(clean, SweepConfig());
    REQUIRE(adc.EnsureInitialized().ok());
    REQUIRE(prepare(adc) == Error::Ok);
    const int before = clean.state.transfers;
    CHECK_EQ(op(adc), Error::Ok);
    frames = clean.state.transfers - before;
  }
  CHECK(frames > 0);
  for (int i = 0; i < 2 * frames; ++i) {
    MinimalPort port;
    Adc adc(port, SweepConfig());
    REQUIRE(adc.EnsureInitialized().ok());
    REQUIRE(prepare(adc) == Error::Ok);
    // Each frame fails twice: once before it reaches the chip, once after the chip took it.
    port.state.fail_transfer = port.state.transfers + (i % frames);
    port.state.fail_after_clocking = i >= frames;
    const Error err = op(adc);
    if (err != Error::Bus) {
      std::printf("    %s: fault %d of %d, got %s\n", name, i + 1, 2 * frames,
                  ads7952::ToString(err));
    }
    CHECK_EQ(err, Error::Bus);
    if (adc.IsInitialized()) {
      const auto after = adc.ReadChannel(9);
      if (!after.ok()) {
        std::printf("    %s: fault %d, next read %s\n", name, i + 1,
                    ads7952::ToString(after.error));
      }
      CHECK_EQ(after.value, port.state.chip.CodeOf(9, port.state.chip.two_vref));
    }
  }
}

Error Nothing(Adc& adc) {
  (void)adc;
  return Error::Ok;
}

Error InAuto1(Adc& adc) {
  return adc.ReadAuto1().error;
}

} // namespace

TEST_CASE("Every frame of every conversion can fail and is reported as Bus") {
  for (const auto prepare : {&Nothing, &InAuto1}) {
    SweepBusFaults("ReadChannel", prepare, [](Adc& a) { return a.ReadChannel(3).error; });
    SweepBusFaults("ReadChannels", prepare, [](Adc& a) {
      const std::array<uint8_t, 3> chans{1, 2, 11};
      std::array<uint16_t, 3> codes{};
      return a.ReadChannels(chans, codes).error;
    });
    SweepBusFaults("ReadAuto1", prepare, [](Adc& a) { return a.ReadAuto1().error; });
    SweepBusFaults("ReadAuto2", prepare, [](Adc& a) { return a.ReadAuto2().error; });
    SweepBusFaults("TraceFrames", prepare, [](Adc& a) {
      std::array<uint16_t, 4> words{};
      return a.TraceFrames(4, words).error;
    });
    SweepBusFaults("ReadGpio", prepare, [](Adc& a) { return a.ReadGpio().error; });
    SweepBusFaults("CheckScale", prepare, [](Adc& a) { return a.CheckScale().error; });
  }
}

TEST_CASE("Every frame of every setting can fail and is reported as Bus") {
  for (const auto prepare : {&Nothing, &InAuto1}) {
    SweepBusFaults("SetRange", prepare,
                   [](Adc& a) { return a.SetRange(ads7952::Range::TwoVref).error; });
    SweepBusFaults("SetAlarmThresholds", prepare,
                   [](Adc& a) { return a.SetAlarmThresholds(7, {10, 4000}).error; });
    for (const Verify v : {Verify::None, Verify::ReadBack}) {
      SweepBusFaults("SetAuto1Channels", prepare,
                     [v](Adc& a) { return a.SetAuto1Channels(0x00F0, v).error; });
      SweepBusFaults("SetAuto2LastChannel", prepare,
                     [v](Adc& a) { return a.SetAuto2LastChannel(3, v).error; });
      SweepBusFaults("SetGpioOutputs", prepare,
                     [v](Adc& a) { return a.SetGpioOutputs(0x02, v).error; });
    }
    SweepBusFaults("ApplyConfig", prepare, [](Adc& a) {
      Config c = SweepConfig();
      c.gpio.output_mask = 0x07;
      c.gpio.output_levels = 0x04;
      c.auto1_channels = 0x0003;
      c.auto2_last_channel = 2;
      c.alarms[1] = {8, 2000};
      c.range = ads7952::Range::TwoVref;
      return a.ApplyConfig(c).error;
    });
    SweepBusFaults("Deinitialize", prepare, [](Adc& a) { return a.Deinitialize().error; });
  }
}

TEST_CASE("Every frame of EnsureInitialized can fail and is reported as Bus") {
  Config c = SweepConfig();
  c.alarms[2] = {100, 3000};
  c.alarms[9].high_code = 1000;
  int frames = 0;
  {
    MinimalPort clean;
    Adc adc(clean, c);
    REQUIRE(adc.EnsureInitialized().ok());
    frames = clean.state.transfers;
  }
  CHECK_EQ(frames, 13 + 4 + 3 + 3); // + 3: the scale check
  for (int i = 0; i < frames; ++i) {
    MinimalPort port;
    port.state.fail_transfer = i;
    Adc adc(port, c);
    CHECK_EQ(adc.EnsureInitialized().error, Error::Bus);
    CHECK(!adc.IsInitialized());
    CHECK(adc.EnsureInitialized().ok()); // retried from the beginning
  }
}

TEST_CASE("A frame lost on its way to the chip never yields a result for the wrong channel") {
  // Each frame of a read is lost in turn: the chip never sees it and MISO floats high. The call
  // fails, or returns the chip's real code for the channel asked for.
  for (int op = 0; op < 3; ++op) {
    for (int i = 0; i < 14; ++i) {
      MinimalPort port;
      Adc adc(port, SweepConfig());
      REQUIRE(adc.EnsureInitialized().ok());
      port.state.lose_transfer = port.state.transfers + i;
      if (op == 0) {
        const auto r = adc.ReadChannel(6);
        CHECK(!r.ok() || r.value == port.state.chip.CodeOf(6, false));
      } else if (op == 1) {
        const std::array<uint8_t, 3> chans{6, 7, 8};
        std::array<uint16_t, 3> codes{};
        const auto r = adc.ReadChannels(chans, codes);
        for (std::size_t k = 0; r.ok() && k < chans.size(); ++k) {
          CHECK_EQ(codes[k], port.state.chip.CodeOf(chans[k], false));
        }
      } else {
        const auto r = adc.ReadAuto1();
        for (uint8_t ch = 0; r.ok() && ch < 12; ++ch) {
          CHECK(!r.value.Has(ch) || r.value.code[ch] == port.state.chip.CodeOf(ch, false));
        }
      }
    }
  }
}

TEST_CASE("A failed mask frame is sent again, so the chip never takes a command as its mask") {
  // Found by the sweep above: the chip saw the Auto-1 request but not the mask, and took the next
  // Manual word as the mask (channel select lost, sequence corrupted).
  MinimalPort port;
  Adc adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1;
  CHECK_EQ(adc.SetAuto1Channels(0x0030).error, Error::Bus);
  CHECK_EQ(port.state.chip.auto1_mask, 0x0030U); // the repeat delivered it
  CHECK_EQ(adc.ReadChannel(9).value, port.state.chip.CodeOf(9, false));
  // The request lost instead: the repeated mask reads as a "continue" word.
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(adc.SetAuto1Channels(0x0003).error, Error::Bus);
  CHECK_EQ(port.state.chip.auto1_mask, 0x0030U);
  CHECK_EQ(port.state.chip.reserved_words, 0);
}

TEST_CASE("A failed alarm frame is followed by an exit, so later words are not thresholds") {
  MinimalPort port;
  Adc adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1; // the low threshold of channel 5
  CHECK_EQ(adc.SetAlarmThresholds(5, {800, 3000}).error, Error::Bus);
  CHECK_EQ(port.state.chip.alarm_low[4], 0U); // the repair wrote channel 4's own value
  CHECK_EQ(port.state.chip.alarm_high[5], ads7952::reg::kAlarmHighReset);
  CHECK_EQ(adc.ReadChannel(9).value, port.state.chip.CodeOf(9, false));
  CHECK(adc.SetAlarmThresholds(5, {800, 3000}).ok()); // a retry completes it
  CHECK_EQ(port.state.chip.alarm_low[5], 200U);
  port.state.fail_transfer = port.state.transfers; // the group frame lost
  CHECK_EQ(adc.SetAlarmThresholds(0, {8, 4000}).error, Error::Bus);
  CHECK_EQ(port.state.chip.alarm_low[0], 0U);
  CHECK_EQ(adc.ReadChannel(9).value, port.state.chip.CodeOf(9, false));
}
