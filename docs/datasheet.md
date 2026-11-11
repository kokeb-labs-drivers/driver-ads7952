# Datasheet map

The driver follows the ADS7952 datasheet. This page says which revision, where to get it, and where each part of
the driver is described in it, so you can check any behaviour against the source. Section names and page numbers
are the datasheet's own (the printed page numbers).


<!-- generated: datasheet-map -->
- **Datasheet: ADS79xx 12-, 10-, 8-Bit, 1-MSPS, 16-, 12-, 8-, 4-Channel, Single-Ended, Serial Interface ADCs**, Texas
  Instruments SLAS605, revision C (2018-07). Get it from the [vendor page](https://www.ti.com/product/ADS7952) or as a
  [PDF](https://www.ti.com/lit/ds/symlink/ads7952.pdf).
  SHA-256 of the file used: `e7922065e4d1691d2c1b9599273a07d1854331a47f7818d70fe830e9a8811d8b`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| The family, its channel counts and resolutions | 1 Features; 3 Description; 5 Device Comparison Table | [p. 1](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=1) | `Variant`, `ChannelCount()` | [Hardware](hardware.md), [Configuration](configuration.md) |
| Pins, packages and the GPIOs each package brings out | 6 Pin Configuration and Functions | [p. 5](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=5) | `Package`, `GpioConfig` | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Absolute limits of the inputs and supplies | 7.1 Absolute Maximum Ratings | [p. 10](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=10) | `Config::va_min_mv` (documented limits only: no register) | [Reference, range and supplies](reference-and-supplies.md), [Hardware](hardware.md) |
| Supplies, reference and SCLK limits | 7.3 Recommended Operating Conditions | [p. 10](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=10) | `Config::vref_mv`, `Config::va_min_mv`, `Validate()` | [Hardware](hardware.md), [Configuration](configuration.md), [Porting](porting.md), [Reference, range and supplies](reference-and-supplies.md) |
| Input ranges, power-up time and the invalid first conversion | 7.6 Electrical Characteristics (ADS7950 to ADS7953) | [p. 12](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=12) | `Range`, `kPowerUpUs`, `EnsureInitialized()`, `Validate()` | [Hardware](hardware.md), [Contract](contract.md), [Configuration](configuration.md), [Reference, range and supplies](reference-and-supplies.md) |
| Serial timing, CS high time and the 16-clock frame | 7.9 Timing Requirements; Figures 1 and 2 | [p. 17](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=17) | `SpiInterface`, `kFrameBytes` | [Porting](porting.md), [Hardware](hardware.md) |
| Frame, reply word, GPIO refresh and alarm timing | 8.1 Overview | [p. 28](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=28) | `reg::ReplyAddress()`, `reg::ReplyCode()`, `SetGpioOutputs()` | [Hardware](hardware.md), [Architecture](architecture.md), [Contract](contract.md) |
| The external reference and its 10 µF capacitor | 8.3.1 Reference | [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29) | `Config::vref_mv`, `ScaleMonitor` | [Reference, range and supplies](reference-and-supplies.md), [Hardware](hardware.md) |
| Power-down through DI05 | 8.3.2 Power Saving | [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29) | `Deinitialize()` | [API guide](api-guide.md), [Contract](contract.md) |
| Channel sequencing modes and the program registers' reset values | 8.4.1 Channel Sequencing Modes; 8.4.2 Device Programming and Mode Control | [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29) | `ResetConfig()`, `Config::auto1_channels`, `Config::auto2_last_channel` | [Hardware](hardware.md), [Configuration](configuration.md) |
| Power-up and the programming sequence | 8.4.3 Device Power-Up Sequence, Figure 49 | [p. 30](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=30) | `EnsureInitialized()`, `ApplyConfig()`, `Error::DeviceReset`, `Config::park_channel` | [Contract](contract.md), [Troubleshooting](troubleshooting.md), [Status and limitations](limitations.md) |
| Manual mode and the two-frame read latency | 8.4.4 Operating in Manual Mode, Figures 50 and 51, Table 1 | [p. 31](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=31) | `ReadChannel()`, `ReadChannels()`, `TraceFrames()`, `SetRange()` | [API guide](api-guide.md), [Architecture](architecture.md), [Hardware](hardware.md), [Troubleshooting](troubleshooting.md) |
| Auto-1 mode and its program register | 8.4.5 Operating in Auto-1 Mode, Figures 52 to 54, Tables 2 to 4 | [p. 34](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=34) | `ReadAuto1()`, `SetAuto1Channels()`, `Error::SequenceMismatch` | [API guide](api-guide.md), [Architecture](architecture.md), [Hardware](hardware.md) |
| Auto-2 mode and its program register | 8.4.6 Operating in Auto-2 Mode, Figures 55 to 57, Tables 5 and 6 | [p. 38](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=38) | `ReadAuto2()`, `SetAuto2LastChannel()` | [API guide](api-guide.md), [Hardware](hardware.md) |
| The "continue" word | 8.4.7 Continued Operation in a Selected Mode, Table 7 | [p. 41](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=41) | `reg::kContinue`, `TraceFrames()` | [Architecture](architecture.md), [Hardware](hardware.md) |
| Output codes and the LSB size | 8.5.1 Digital Output, Table 8 | [p. 41](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=41) | `CodeToMicrovolts()`, `ToMicrovolts()`, `Bank::range`, `CheckScaleCode()` | [API guide](api-guide.md), [Configuration](configuration.md), [Reference, range and supplies](reference-and-supplies.md) |
| GPIO program register and the register reset | 8.5.2 GPIO Registers, Figure 58, Table 11 | [p. 42](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=42) | `GpioConfig`, `AlarmOutput`, `ReadGpio()`, `SetGpioOutputs()`, `EnsureInitialized()` | [Hardware](hardware.md), [Configuration](configuration.md), [API guide](api-guide.md), [Contract](contract.md), [Reference, range and supplies](reference-and-supplies.md) |
| Alarm thresholds | 8.5.3 Alarm Thresholds for GPIO Pins, Figure 59, Tables 12 and 13 | [p. 43](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=43) | `AlarmThreshold`, `SetAlarmThresholds()` | [API guide](api-guide.md), [Configuration](configuration.md), [Hardware](hardware.md) |
| Analog input, source impedance and MXO to AINP | 9.1.1 Analog Input, Figures 60 and 61 | [p. 46](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=46) | none: board design | [Reference, range and supplies](reference-and-supplies.md), [Hardware](hardware.md) |
| Supply range, decoupling and +VA at or above +VBD | 10 Power Supply Recommendations | [p. 51](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=51) | `Config::va_min_mv` | [Reference, range and supplies](reference-and-supplies.md), [Hardware](hardware.md) |
| Layout of grounds, supply and reference capacitors | 11.1 Layout Guidelines; 11.2 Layout Examples, Figures 69 and 70 | [p. 52](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=52) | none: board design | [Reference, range and supplies](reference-and-supplies.md) |
<!-- /generated -->

## When the datasheet and the driver differ

The driver is written against the revision above. If you have a newer revision, compare its revision history with
the topics above; a change to a pin, a register, a timing or a value the driver uses is a bug report for this
repository. Where the driver deliberately goes beyond the datasheet, the [Contract](contract.md) says so.

The repository never contains the PDF itself: vendors own their documents. Download it from the vendor, and compare
its SHA-256 with the one above to confirm you have the same file.
