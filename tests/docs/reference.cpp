/**
 * @file reference.cpp
 * @brief The "Reference, range and supplies" page's examples, compiled by the host build (never
 * run).
 */
#include <cstdint>

#include "ads7952.hpp"
#include "my_port.hpp"

// --8<-- [start:config]
// A 5 V sensor board: a 2.5 V reference on REFP, and REFP also wired to channel 11.
ads7952::Config SensorBoardConfig() {
  ads7952::Config c{};
  c.range = ads7952::Range::TwoVref; // full scale 2 x 2.5 V = 5.0 V, one LSB = 1.22 mV
  c.vref_mv = 2500;                  // the reference's nominal value
  c.va_min_mv = 5000;                // the lowest +VA the rail guarantees: 2 x 2500 <= 5000
  c.auto1_channels = 0x0FFF;         // the scan includes the monitor channel
  // REFP on channel 11 reads 2048 in Range 2 and 4095 in Range 1: a lost range cannot pass.
  c.monitor = {.channel = 11, .expected_uv = 2500000U, .tolerance_uv = 100000U}; // 2.5 V +/- 100 mV
  return c;
}
// --8<-- [end:config]

// --8<-- [start:bank]
// One scan, proved before any value leaves the driver layer. CheckScaleCode() costs no frame.
ads7952::Error ReadProvedBank(ads7952::ADS7952<MyPort>& adc, ads7952::Bank& out) {
  const auto bank = adc.ReadAuto1();
  if (!bank) {
    return bank.error; // DeviceReset / SequenceMismatch: the chip was reset; start it again
  }
  const ads7952::Error scale = adc.CheckScaleCode(bank.value.code[11]);
  if (scale != ads7952::Error::Ok) {
    return scale; // RangeMismatch (x2 or /2) or ScaleMismatch (reference): do not publish
  }
  out = bank.value;
  return ads7952::Error::Ok;
}
// --8<-- [end:bank]

// --8<-- [start:ratiometric]
// A ratiometric sensor (output = a fraction of its own supply) read on channel 3, its supply
// measured on channel 9 through a divider of exactly 1/2. Both conversions come from one bank, so
// the reference and the range cancel: the result does not depend on VREF or on the 5 V rail.
uint32_t SensorFractionPpm(const ads7952::ADS7952<MyPort>& adc, const ads7952::Bank& bank) {
  const uint64_t signal_uv = adc.ToMicrovolts(bank, 3);
  const uint64_t supply_uv = 2U * static_cast<uint64_t>(adc.ToMicrovolts(bank, 9));
  if (supply_uv == 0U) {
    return 0U; // no supply: the caller treats the sensor as absent
  }
  return static_cast<uint32_t>((signal_uv * 1000000U) / supply_uv); // 100000 = 10 % of supply
}
// --8<-- [end:ratiometric]
