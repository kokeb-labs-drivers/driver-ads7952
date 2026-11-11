/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "ads7952.hpp"

namespace {

// A port that answers every read with 0: enough to instantiate the whole driver template.
class NullPort : public ads7952::SpiInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    (void)tx;
    for (std::size_t i = 0; rx != nullptr && i < len; ++i) {
      rx[i] = 0;
    }
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

} // namespace

int main() {
  NullPort port;
  ads7952::ADS7952<NullPort> sensor(port);
  // CHIP_ID reads 0, so EnsureInitialized() must fail with DeviceIdMismatch: proof the template
  // compiled and ran.
  const auto r = sensor.EnsureInitialized();
  return (r.error == ads7952::Error::DeviceIdMismatch && ads7952::GetDriverVersion()[0] != '\0')
             ? 0
             : 1;
}
