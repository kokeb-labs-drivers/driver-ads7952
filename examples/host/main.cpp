/**
 * @file main.cpp
 * @brief The ADS7952 driver on your PC: start a simulated chip, read one channel and scan the
 * Auto-1 sequence.
 *
 * No hardware and no SDK: the host port (host_ads7952_port.hpp) connects the real driver to the
 * chip model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "ads7952.hpp"
#include "host_ads7952_port.hpp"

int main() {
  HostAds7952Port port;
  ads7952::Config config{};
  config.auto1_channels = 0x0ACF;                      // channels 0-3, 6, 7, 9 and 11
  ads7952::ADS7952<HostAds7952Port> adc(port, config); // no I/O yet

  std::printf("ADS7952 driver %s on the host\n", ads7952::GetDriverVersion());
  if (const auto r = adc.EnsureInitialized(); !r) {
    std::printf("EnsureInitialized failed: %s\n", ads7952::ToString(r.error));
    return 1;
  }

  const auto one = adc.ReadChannel(3);
  if (!one) {
    std::printf("ReadChannel failed: %s\n", ads7952::ToString(one.error));
    return 1;
  }
  std::printf("CH3: %u (%lu uV)\n", static_cast<unsigned>(one.value),
              static_cast<unsigned long>(adc.ToMicrovolts(one.value)));

  for (int pass = 0; pass < 2; ++pass) {
    const auto bank = adc.ReadAuto1();
    if (!bank) {
      std::printf("ReadAuto1 failed: %s\n", ads7952::ToString(bank.error));
      return 1;
    }
    for (uint8_t ch = 0; ch < adc.GetChannelCount(); ++ch) {
      if (bank.value.Has(ch)) {
        // ToMicrovolts(bank, ch) converts with the range the bank was made in (Bank::range).
        std::printf("CH%u=%u (%lu uV) ", static_cast<unsigned>(ch),
                    static_cast<unsigned>(bank.value.code[ch]),
                    static_cast<unsigned long>(adc.ToMicrovolts(bank.value, ch)));
      }
    }
    std::printf("\n");
  }
  return 0;
}
