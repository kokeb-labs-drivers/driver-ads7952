# ADS7952 ESP32 examples

ESP-IDF applications that run the ADS7952 driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | ADS7952 | Notes |
| --- | --- | --- |
| GPIO6 | SCLK | SPI2, mode 0, 1 MHz in the example (up to 20 MHz) |
| GPIO7 | SDI (MOSI) | |
| GPIO2 | SDO (MISO) | |
| GPIO10 | CS | Driven by spi_master: one 16-bit transfer per frame, CS high between frames |
| 3V3 | +VBD | Digital I/O supply, 1.7 V to +VA |
| 5 V or 3V3 | +VA | Analog supply, 2.7 V to 5.25 V; decouple at the pins |
| (reference) | REFP, REFM | 2.5 V reference (REF5025 or similar), 10 uF between REFP and REFM |
| GND | AGND, BDGND | |

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh ads7952_basic_example Debug
./scripts/flash_app.sh flash_monitor ads7952_basic_example Debug
```

`idf.py menuconfig` shows "ADS7950 / ADS7951 / ADS7952 / ADS7953 SAR ADC" under Component config: the part, the
package, the range, the reference and supply voltages and the Auto-1 and Auto-2 sequences that
`ads7952::KconfigDefaults()` returns.

## Expected output

```text
I (312) ads7952_example: ADS7952 driver 0.1.0
ads7952: initialised
I (318) ads7952_example: initialised, 12 channels
I (320) ads7952_example: CH0  164   100097 uV
I (320) ads7952_example: CH1  409   249633 uV
```

An `EnsureInitialized failed: DeviceIdMismatch` line means the replies did not carry the channel addresses the
driver selected: check SDI (MOSI), CS and the SPI mode first. `NoDevice` (last reply 0xFFFF) means nothing drives
MISO: check the supply and the SDO wire. Not verified on hardware.

## Files

| File | What it is |
| --- | --- |
| [`main/ads7952_basic_example.cpp`](main/ads7952_basic_example.cpp) | The example app |
| [`main/esp32_ads7952_port.hpp`](main/esp32_ads7952_port.hpp) | The ESP-IDF port: bus, delay, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
