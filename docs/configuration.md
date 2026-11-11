# Configuration

Every field of `ads7952::Config`, how a configuration is checked, and the optional Kconfig layer.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - The family, its channel counts and resolutions: 1 Features; 3 Description; 5 Device Comparison Table, [p. 1](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=1)
    - Supplies, reference and SCLK limits: 7.3 Recommended Operating Conditions, [p. 10](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=10)
    - Input ranges, power-up time and the invalid first conversion: 7.6 Electrical Characteristics (ADS7950 to ADS7953), [p. 12](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=12)
    - Channel sequencing modes and the program registers' reset values: 8.4.1 Channel Sequencing Modes; 8.4.2 Device Programming and Mode Control, [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29)
    - Output codes and the LSB size: 8.5.1 Digital Output, Table 8, [p. 41](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=41)
    - GPIO program register and the register reset: 8.5.2 GPIO Registers, Figure 58, Table 11, [p. 42](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=42)
    - Alarm thresholds: 8.5.3 Alarm Thresholds for GPIO Pins, Figure 59, Tables 12 and 13, [p. 43](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=43)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Fields

`Config{}` holds the reset values of an ADS7952 (Tables 1 to 13), so it changes nothing on a freshly reset chip;
`ResetConfig(variant)` gives the same for the other parts. `EnsureInitialized()` resets the chip and writes every
field; `ApplyConfig()` on a running driver writes the fields that changed. Fields with a unit carry it in the name.

| Field | Default | Range | Register |
| --- | --- | --- | --- |
| `variant` | `Variant::Ads7952` | `Ads7950`, `Ads7951`, `Ads7952`, `Ads7953` | none: the channel count (4, 8, 12, 16) |
| `package` | `Package::Tssop` | `Tssop`, `Vqfn` (GPIO0 only) | GPIO register DI03:01 on VQFN |
| `range` | `Range::Vref` | `Vref`, `TwoVref` (2 x `vref_mv` <= `va_min_mv`) | mode control DI06 |
| `vref_mv` | 2500 | 2000 to 3000 | none: converts codes |
| `va_min_mv` | 5000 | 2700 to 5250: the lowest +VA the board guarantees | none: checks Range 2 |
| `auto1_channels` | 0x0FFF | non-zero, channels of the variant | Auto-1 program register |
| `auto2_last_channel` | 11 | a channel of the variant | Auto-2 program register DI09:06 |
| `gpio.output_mask` | 0 | bit n: GPIOn an output; not a pin with a function | GPIO register DI03:00 |
| `gpio.output_levels` | 0 | output pins only | mode control DI03:00 |
| `gpio.alarm_output` | `AlarmOutput::None` | the five codes of Table 11 | GPIO register DI06:04 |
| `gpio.gpio2_range_input` | `false` | TSSOP only | GPIO register DI07 |
| `gpio.gpio3_power_down_input` | `false` | TSSOP only | GPIO register DI08 |
| `alarms[ch]` | `{0, 4095}`: no alarm | codes 0 to 4095; only channels of the variant | alarm program registers (upper 10 bits) |
| `monitor` | `{}`: none | a channel with a voltage derived from REFP, its expected value and tolerance in µV | none: checks the scale ([Reference, range and supplies](reference-and-supplies.md)) |
| `park_channel` | `kNoPark`: none | 1 to the variant's last channel | none: after `ReadChannel(0)` the chip rests there, so a reset shows ([Status and limitations](limitations.md#limits-of-the-design)) |

Two fields of the mode control word are not in `Config`: DI04 (the reply carries the GPIO levels) is set only for one
frame inside `ReadGpio()` and `Verify::ReadBack`, and DI05 (power-down) only by `Deinitialize()`.

Start from `Config{}` and change fields:

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## Validation

`ads7952::Validate(config)` checks a configuration without touching the chip, and `EnsureInitialized()` and
`ApplyConfig()` call it before any I/O. It returns `Error::InvalidArgument` when an enum holds a value that is not
an enumerator, a voltage is outside section 7.3, Range 2 is asked for with 2 x `vref_mv` above `va_min_mv` (section 7.6),
the Auto-1 mask is empty or names a channel the variant lacks, the Auto-2 last channel is not a channel, a threshold
is above 4095 or set on a channel the variant lacks, a pin with a function is also an output, or a level is set on a
pin that is not an output. It returns `Error::Unsupported` for a GPIO1 to GPIO3 setting on the VQFN package.

It also returns `InvalidArgument` for a scale monitor on a channel the variant lacks, with a zero tolerance, with a
window that touches code 0 or 4095, or with windows of the two ranges that touch (`ValidateMonitor()`).

**Range 2 and the supply.** `va_min_mv` is the lowest +VA the board guarantees under load, not the nominal rail: Range 2
is specified only while 2 x VREF <= +VA. With a 2.5 V reference and +VA measured at 4.89 V to 4.91 V on a nominal 5 V
rail, `Validate()` refuses Range 2 until the supply or the reference changes. The choices are in [Reference, range and
supplies](reference-and-supplies.md#choosing-the-range).

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **ADS7950 / ADS7951 / ADS7952 / ADS7953 SAR ADC** when the driver is a
component of the project ([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The
options reach the code only through [`inc/ads7952_kconfig.hpp`](../inc/ads7952_kconfig.hpp), never through a driver
signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_ADS7952_VARIANT_*` | ADS7952 | `variant` |
| `CONFIG_ADS7952_PACKAGE_VQFN` | n | `package` |
| `CONFIG_ADS7952_RANGE_2VREF` | n | `range` |
| `CONFIG_ADS7952_VREF_MV` | 2500 | `vref_mv` |
| `CONFIG_ADS7952_VA_MIN_MV` | 5000 | `va_min_mv` |
| `CONFIG_ADS7952_AUTO1_CHANNELS` | every channel of the part | `auto1_channels` |
| `CONFIG_ADS7952_AUTO2_LAST_CHANNEL` | the part's last channel | `auto2_last_channel` |

GPIO and alarm settings are not Kconfig options: set them in code. Without Kconfig (plain CMake, other platforms)
`KconfigDefaults()` returns `Config{}`. Both cases are host tests.

Next: [Porting](porting.md).
