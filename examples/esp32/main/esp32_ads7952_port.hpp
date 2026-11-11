/**
 * @file esp32_ads7952_port.hpp
 * @brief ESP-IDF port for the ADS7952 driver: spi_master for the bus (CS parked high first),
 * esp_rom delays and ESP_LOG for the log hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Ads7952Pins; the defaults match
 * the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1).
 */
#ifndef ADS7952_ESP32_ADS7952_PORT_HPP
#define ADS7952_ESP32_ADS7952_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "ads7952.hpp"

/// The port's default SPI clock: 1 MHz for bring-up on jumper wires.
inline constexpr int kEsp32Ads7952ClockHz = 1'000'000;
/// The port's SPI mode.
inline constexpr int kEsp32Ads7952SpiMode = 0;
/// Bytes per transaction: one 16-bit frame, clocked as one continuous 16-bit word by the
/// spi_master peripheral (no idle SCLK between the two bytes).
inline constexpr int kEsp32Ads7952FrameBytes = 2;
// The bus requirements of the driver (handbook KB-CPP-123), checked at compile time.
static_assert(kEsp32Ads7952ClockHz <= static_cast<int>(ads7952::kMaxClockHz),
              "the default clock is above the ADS795x's f(SCLK) max");
static_assert(kEsp32Ads7952SpiMode == ads7952::kSpiMode, "the ADS795x latches SDI in SPI mode 0");
static_assert(ads7952::kMsbFirst, "this port sends MSB first (no SPI_DEVICE_BIT_LSBFIRST)");
static_assert(kEsp32Ads7952FrameBytes == static_cast<int>(ads7952::kFrameBytes) &&
                  ads7952::kContinuousWord,
              "one polling transaction per 16-bit frame keeps the 16 clocks together");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32Ads7952Pins {
  spi_host_device_t host = SPI2_HOST;
  int sclk = 6;
  int mosi = 7;
  int miso = 2;
  int cs = 10;
  /// Up to `ads7952::kMaxClockHz`, checked at start; the rate also sets the acquisition time
  /// (porting.md).
  int clock_hz = kEsp32Ads7952ClockHz;
};

/// The ADS7952 SPI port on ESP-IDF, with every optional hook.
class Esp32Ads7952Port : public ads7952::SpiInterface<Esp32Ads7952Port> {
public:
  explicit Esp32Ads7952Port(const Esp32Ads7952Pins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (device_ != nullptr) {
      return true;
    }
    if (pins_.clock_hz <= 0 || pins_.clock_hz > static_cast<int>(ads7952::kMaxClockHz)) {
      ESP_LOGE(kTag, "clock outside the ADS795x's bus requirements");
      return false;
    }
    // CS is the chip's only control input: drive it to its idle level (high) before its output is
    // enabled and before the SPI peripheral takes the pin, so the chip sees no CS falling edge it
    // was not sent (handbook KB-HW-018).
    gpio_config_t cs{};
    cs.pin_bit_mask = uint64_t{1} << static_cast<uint32_t>(pins_.cs);
    cs.mode = GPIO_MODE_OUTPUT;
    if (gpio_set_level(static_cast<gpio_num_t>(pins_.cs), 1) != ESP_OK ||
        gpio_config(&cs) != ESP_OK) {
      return false;
    }
    spi_bus_config_t bus{};
    bus.mosi_io_num = pins_.mosi;
    bus.miso_io_num = pins_.miso;
    bus.sclk_io_num = pins_.sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kEsp32Ads7952FrameBytes;
    const esp_err_t bus_err = spi_bus_initialize(pins_.host, &bus, SPI_DMA_DISABLED);
    if (bus_err != ESP_OK && bus_err != ESP_ERR_INVALID_STATE) { // INVALID_STATE: bus already up
      ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(bus_err));
      return false;
    }
    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = pins_.clock_hz;
    dev.mode = kEsp32Ads7952SpiMode; // SDI latched on the rising edge, SDO changes on the falling
                                     // edge (Figure 2)
    dev.spics_io_num = pins_.cs;
    dev.queue_size = 1;
    if (spi_bus_add_device(pins_.host, &dev, &device_) != ESP_OK) {
      device_ = nullptr;
      return false;
    }
    return true;
  }

  // One 16-bit frame: CS low for both bytes and high after, as the chip needs for each conversion.
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    if (device_ == nullptr || len != static_cast<std::size_t>(kEsp32Ads7952FrameBytes)) {
      return false;
    }
    spi_transaction_t t{};
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(device_, &t) == ESP_OK;
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  void Log(ads7952::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = level == ads7952::LogLevel::Error     ? ESP_LOG_ERROR
                                      : level == ads7952::LogLevel::Warning ? ESP_LOG_WARN
                                      : level == ads7952::LogLevel::Info    ? ESP_LOG_INFO
                                                                            : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  static constexpr const char* kTag = "ads7952";
  Esp32Ads7952Pins pins_;
  spi_device_handle_t device_ = nullptr;
};

#endif // ADS7952_ESP32_ADS7952_PORT_HPP
