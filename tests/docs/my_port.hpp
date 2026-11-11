/**
 * @file my_port.hpp
 * @brief The SPI port shown on the Quickstart and Porting pages, compiled by the host build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux spidev).
 */
#ifndef ADS7952_MY_PORT_HPP
#define ADS7952_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "ads7952_spi_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_spi_init() noexcept; // mode 0, <= 20 MHz, 8- or 16-bit words
bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx,
                           std::size_t len) noexcept; // CS low for both bytes
void platform_delay_us(uint32_t us) noexcept;         // busy-wait or sleep

// --8<-- [start:port]
class MyPort : public ads7952::SpiInterface<MyPort> {
public:
  // Bring the bus up once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_spi_init();
    }
    return ready_;
  }

  // One 16-bit frame: CS low for both bytes, high after. The driver always passes len = 2.
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }

  // Wait at least `us` microseconds: used once, for the power-up time after the reset.
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // ADS7952_MY_PORT_HPP
