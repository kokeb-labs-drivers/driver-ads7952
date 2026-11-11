/**
 * @file test_errors.cpp
 * @brief Each chip-specific error is produced by a real call of the driver, and the base errors the
 * driver documents as never returned stay so.
 */
#include <array>
#include <cstdint>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"
#include "test_harness.hpp"

using ads7952::Error;
using ads7952::Verify;
using ads7952_test::MinimalPort;
using ads7952_test::Miso;

namespace {
using Adc = ads7952::ADS7952<MinimalPort>;
} // namespace

TEST_CASE("NoDevice: EnsureInitialized with MISO floating high") {
  MinimalPort port;
  port.state.chip.miso = Miso::FloatHigh;
  Adc adc(port);
  CHECK_EQ(adc.EnsureInitialized().error, Error::NoDevice);
}

TEST_CASE("DeviceIdMismatch: EnsureInitialized with one identity reply wrong") {
  MinimalPort port;
  port.state.replace_transfer = 12; // the reply that must carry channel 5
  port.state.replace_word = 0x4123;
  Adc adc(port);
  CHECK_EQ(adc.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("ChannelMismatch: ReadChannel with the select lost") {
  MinimalPort port;
  Adc adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  port.state.lose_transfer = port.state.transfers; // the chip stays on channel 5
  CHECK_EQ(adc.ReadChannel(7).error, Error::ChannelMismatch);
  CHECK(!adc.IsInitialized());
}

TEST_CASE("SequenceMismatch: ReadAuto1 with one reply from another channel") {
  MinimalPort port;
  Adc adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  REQUIRE(adc.ReadAuto1().ok());
  port.state.replace_transfer = port.state.transfers + 4;
  port.state.replace_word = 0xF000; // channel 15: not in the sequence, not the reset channel
  CHECK_EQ(adc.ReadAuto1().error, Error::SequenceMismatch);
  CHECK(!adc.IsInitialized());
}

TEST_CASE("DeviceReset: ReadChannel after the chip was reset behind the driver") {
  MinimalPort port;
  Adc adc(port);
  REQUIRE(adc.EnsureInitialized().ok());
  REQUIRE(adc.ReadChannel(7).ok());
  port.state.chip.Brownout(); // back to channel 0
  (void)adc.ReadChannel(7);   // channel 7, acquired before the brown-out, still in flight
  CHECK_EQ(adc.ReadChannel(7).error, Error::DeviceReset);
  CHECK(!adc.IsInitialized());
  CHECK_EQ(static_cast<int>(Error::DeviceReset), 22);
}

TEST_CASE("WriteMismatch: SetGpioOutputs read back with the pin held by the board") {
  ads7952::Config c{};
  c.gpio.output_mask = 0x01;
  MinimalPort port;
  Adc adc(port, c);
  REQUIRE(adc.EnsureInitialized().ok());
  port.state.chip.gpio_held = 0x01;
  CHECK_EQ(adc.SetGpioOutputs(0x01, Verify::ReadBack).error, Error::WriteMismatch);
}

TEST_CASE("Unsupported: SetRange while GPIO2 selects the range") {
  ads7952::Config c{};
  c.gpio.gpio2_range_input = true;
  MinimalPort port;
  Adc adc(port, c);
  REQUIRE(adc.EnsureInitialized().ok());
  CHECK_EQ(adc.SetRange(ads7952::Range::TwoVref).error, Error::Unsupported);
}

TEST_CASE("RangeMismatch: GPIO2 selects Range 1 while the Config declares Range 2") {
  ads7952::Config c{};
  c.range = ads7952::Range::TwoVref;
  c.gpio.gpio2_range_input = true;
  c.monitor = {.channel = 11, .expected_uv = 2500000U, .tolerance_uv = 100000U};
  MinimalPort port;
  port.state.chip.input_mv[11] = 2500;
  port.state.chip.gpio_inputs = 0x00; // the board holds GPIO2 low: Range 1
  Adc adc(port, c);
  CHECK_EQ(adc.EnsureInitialized().error, Error::RangeMismatch);
}

TEST_CASE("ScaleMismatch: the monitor input is not the voltage declared") {
  ads7952::Config c{};
  c.range = ads7952::Range::TwoVref;
  c.monitor = {.channel = 11, .expected_uv = 2500000U, .tolerance_uv = 100000U};
  MinimalPort port;
  port.state.chip.input_mv[11] = 0; // reference missing or monitor unwired
  Adc adc(port, c);
  CHECK_EQ(adc.EnsureInitialized().error, Error::ScaleMismatch);
}

TEST_CASE("The base errors documented as not returned keep their values") {
  // Timeout, Crc, Busy and DeviceFault: no call waits, the chip sends no CRC, is never busy and
  // reports no fault. The sweep in test_bus_faults.cpp and the tests above cover every path; the
  // names stay for the shared base set.
  CHECK_EQ(static_cast<int>(Error::Timeout), 4);
  CHECK_EQ(static_cast<int>(Error::Crc), 6);
  CHECK_EQ(static_cast<int>(Error::Busy), 7);
  CHECK_EQ(static_cast<int>(Error::DeviceFault), 8);
  CHECK_EQ(static_cast<int>(Error::NoDevice), 16);
  CHECK_EQ(static_cast<int>(Error::WriteMismatch), 19);
}
