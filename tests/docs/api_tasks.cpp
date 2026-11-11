/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <array>
#include <cstdint>
#include <cstdio>

#include "ads7952.hpp"
#include "my_port.hpp"

using Adc = ads7952::ADS7952<MyPort>;

// --8<-- [start:lifecycle]
ads7952::Error BringUp(Adc& adc) {
  // The one entry point: resets and checks the chip on the first call, then returns Ok with no
  // I/O. After a failure the next call starts again from the beginning.
  const auto r = adc.EnsureInitialized();
  if (!r) {
    std::printf("ADS7952 init: %s (last reply 0x%04X)\n", ads7952::ToString(r.error),
                static_cast<unsigned>(adc.GetLastReply()));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:scan]
// A control loop: the same channels every tick. The first call enters Auto-1 (k + 2 frames),
// every later one is k frames (one Auto-1 word per channel).
ads7952::Error Tick(Adc& adc, std::array<uint16_t, 16>& codes) {
  const auto bank = adc.ReadAuto1();
  if (!bank) {
    return bank.error; // DeviceReset or SequenceMismatch: call EnsureInitialized() again
  }
  codes = bank.value.code; // codes[ch] for every ch in bank.value.mask
  return ads7952::Error::Ok;
}
// --8<-- [end:scan]

// --8<-- [start:sequence]
ads7952::Error UseChannels(Adc& adc, uint16_t mask) {
  // Two frames, plus one scan that checks the chip took the new list; the scan leaves the
  // sequence running, so the next ReadAuto1() costs one frame per channel.
  return adc.SetAuto1Channels(mask, ads7952::Verify::ReadBack).error;
}
// --8<-- [end:sequence]

// --8<-- [start:manual]
ads7952::Error ReadThree(Adc& adc, std::array<uint16_t, 3>& codes) {
  const std::array<uint8_t, 3> channels{4, 7, 9}; // Figure 51: 3 + 2 frames
  return adc.ReadChannels(channels, codes).error;
}
// --8<-- [end:manual]

// --8<-- [start:gpio]
ads7952::Error Indicate(Adc& adc, bool on) {
  // Config::gpio.output_mask must make GPIO2 an output. ReadBack: one more frame, and the level
  // is read back from the pin.
  const auto levels = static_cast<uint8_t>(on ? 0x04U : 0x00U);
  return adc.SetGpioOutputs(levels, ads7952::Verify::ReadBack).error;
}
// --8<-- [end:gpio]

// --8<-- [start:alarm]
ads7952::Error ArmOverRange(Adc& adc) {
  // GPIO0 goes high when channel 0 reads above 3600 or below 400 (Config::gpio.alarm_output must
  // be AlarmOutput::Gpio0HighOrLow). The chip keeps the upper 10 bits of each threshold.
  return adc.SetAlarmThresholds(0, {400, 3600}).error;
}
// --8<-- [end:alarm]

// --8<-- [start:errors]
const char* Describe(ads7952::Error e) {
  switch (e) {
  case ads7952::Error::Bus:
  case ads7952::Error::NoDevice:
  case ads7952::Error::DeviceIdMismatch:
    return "check the wiring: SDI, SDO, SCLK, CS and the supplies";
  case ads7952::Error::DeviceReset:
  case ads7952::Error::ChannelMismatch:
  case ads7952::Error::SequenceMismatch:
    return "the chip was reset or a frame was lost: call EnsureInitialized()";
  default:
    return ads7952::ToString(e);
  }
}
// --8<-- [end:errors]

// --8<-- [start:recover]
ads7952::Error Recover(Adc& adc) {
  // After DeviceReset, ChannelMismatch or SequenceMismatch (the driver is then uninitialised), or a
  // reference monitor that reads wrong: power down, then reset and write every register again.
  (void)adc.Deinitialize();
  return adc.EnsureInitialized().error;
}
// --8<-- [end:recover]
