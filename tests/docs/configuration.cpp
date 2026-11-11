/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "ads7952.hpp"
#include "ads7952_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:custom]
ads7952::Config PressureBoardConfig() {
  ads7952::Config c{};               // an ADS7952 at its reset state
  c.range = ads7952::Range::TwoVref; // 0 to 5 V: needs 2 x vref_mv <= va_min_mv
  c.vref_mv = 2500;
  c.va_min_mv = 5000;        // the lowest +VA the rail guarantees, not its nominal value
  c.auto1_channels = 0x0ACF; // channels 0-3, 6, 7, 9, 11
  c.gpio.alarm_output = ads7952::AlarmOutput::Gpio0High;
  c.alarms[11] = {0, 2200}; // GPIO0 high when channel 11 reads above 2200
  return c;                 // ads7952::Validate(c) checks it without the chip
}

ads7952::Error StartPressureBoard(MyPort& port) {
  static ads7952::ADS7952<MyPort> adc(port, PressureBoardConfig()); // stored, no I/O
  return adc.EnsureInitialized().error; // InvalidArgument if the config is out of range
}
// --8<-- [end:custom]

// --8<-- [start:kconfig]
ads7952::Error StartFromMenuconfig(MyPort& port) {
  static ads7952::ADS7952<MyPort> adc(port, ads7952::KconfigDefaults());
  return adc.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
