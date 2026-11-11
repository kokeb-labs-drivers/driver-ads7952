/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "ads7952.hpp"

int read_input_3(MyPort& port, uint32_t& microvolts) {
  ads7952::ADS7952<MyPort> adc(port); // no I/O yet; Config{} is an ADS7952 at its reset state

  if (auto r = adc.EnsureInitialized(); !r) { // 1. bus up, reset, registers, identity check
    return static_cast<int>(r.error);         //    Bus, NoDevice or DeviceIdMismatch: check wiring
  }
  const auto code = adc.ReadChannel(3); // 2. Manual mode: select, acquire, convert
  if (!code) {
    return static_cast<int>(code.error); //    ChannelMismatch: a reply for another channel
  }
  microvolts = adc.ToMicrovolts(code.value); // 3. 0 to VREF, one LSB = VREF / 4096
  return 0;
}
// --8<-- [end:quickstart]
