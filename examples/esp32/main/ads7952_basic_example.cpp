/**
 * @file ads7952_basic_example.cpp
 * @brief Start the ADS7952 from the menuconfig values, then log one Auto-1 scan twice a second.
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line, then one line per
 * scan with the code of every channel of the sequence. Every Result is checked and logged by name.
 */
#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ads7952.hpp"
#include "ads7952_kconfig.hpp"
#include "esp32_ads7952_port.hpp"

namespace {
constexpr const char* kTag = "ads7952_example";
}

extern "C" void app_main(void) {
  static Esp32Ads7952Port port; // the driver keeps a reference
  static ads7952::ADS7952<Esp32Ads7952Port> adc(port, ads7952::KconfigDefaults()); // no I/O

  ESP_LOGI(kTag, "ADS7952 driver %s", ads7952::GetDriverVersion());
  const auto init = adc.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s (last reply 0x%04X)",
             ads7952::ToString(init.error), static_cast<unsigned>(adc.GetLastReply()));
    return;
  }
  ESP_LOGI(kTag, "initialised, %u channels", static_cast<unsigned>(adc.GetChannelCount()));

  while (true) {
    const auto bank = adc.ReadAuto1();
    if (bank) {
      for (uint8_t ch = 0; ch < adc.GetChannelCount(); ++ch) {
        if (bank.value.Has(ch)) {
          ESP_LOGI(kTag, "CH%u %4u  %7lu uV", static_cast<unsigned>(ch),
                   static_cast<unsigned>(bank.value.code[ch]),
                   static_cast<unsigned long>(adc.ToMicrovolts(bank.value.code[ch])));
        }
      }
    } else {
      ESP_LOGW(kTag, "scan failed: %s", ads7952::ToString(bank.error));
      if (bank.error != ads7952::Error::Bus) {
        (void)adc.Deinitialize(); // the chip lost its sequence or was reset: start it again
        (void)adc.EnsureInitialized();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
