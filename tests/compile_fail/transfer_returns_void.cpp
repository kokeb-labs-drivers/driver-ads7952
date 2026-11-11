/**
 * @file transfer_returns_void.cpp
 * @brief Must NOT compile: a `Transfer()` hook that cannot report a failed transfer is rejected.
 *
 * EXPECT: SpiPort
 */
#include <cstddef>
#include <cstdint>

#include "ads7952.hpp"

class VoidTransferPort : public ads7952::SpiInterface<VoidTransferPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  void Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    (void)tx;
    (void)rx;
    (void)len;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  VoidTransferPort port;
  ads7952::ADS7952<VoidTransferPort> adc(port);
  return static_cast<int>(adc.EnsureInitialized().error);
}
