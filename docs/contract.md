# Contract

What the ADS7952 driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - Input ranges, power-up time and the invalid first conversion: 7.6 Electrical Characteristics (ADS7950 to ADS7953), [p. 12](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=12)
    - Frame, reply word, GPIO refresh and alarm timing: 8.1 Overview, [p. 28](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=28)
    - Power-down through DI05: 8.3.2 Power Saving, [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29)
    - Power-up and the programming sequence: 8.4.3 Device Power-Up Sequence, Figure 49, [p. 30](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=30)
    - GPIO program register and the register reset: 8.5.2 GPIO Registers, Figure 58, Table 11, [p. 42](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=42)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Promises

- The constructor is `noexcept`, does no I/O and cannot fail. No call clocks the bus before `EnsureInitialized()`,
  so the driver can be constructed statically even where MISO may float.
- `EnsureInitialized()` resets the chip, writes the configuration and runs the identity check below. On any failure
  the driver stays uninitialised, and the next call starts again from the reset.
- Every call that touches the chip returns `Error::NotInitialized` before `EnsureInitialized()` succeeds and sends
  nothing. No call starts the chip as a side effect.
- Every port failure propagates as `Error::Bus`. A failed call returns `T{}`; a failed `ReadChannels()` or
  `TraceFrames()` leaves its buffer zeroed, never a partial result.
- A result is returned only from a reply whose address (DO15:12) is the channel it stands for, two frames after the
  frame that chose the channel (Figures 51, 53, 56). A Manual reply with another address is `ChannelMismatch`; an
  Auto-1 or Auto-2 pass that is not one turn of the sequence is `SequenceMismatch`; either one from channel 0, the
  power-on channel (section 8.4.3), is `DeviceReset`. The replies a select or an Auto entry discards must carry the
  Manual channel the chip had settled on.
- After `DeviceReset`, `ChannelMismatch`, `SequenceMismatch` from a running driver, or a failed `CheckScale()`
  (other than `Bus`), the driver is uninitialised: it never writes again from a configuration the chip may have
  lost, and `EnsureInitialized()` resets and programs it again (fail-safe).
- A reply converted in the old range after `SetRange()` is never returned: the next read skips it.
- Every mode word the driver sends has DI11 = 1 and carries the configured range and GPIO levels, with DI04 = 0 and
  DI05 = 0 (except `Deinitialize()`'s), so a chip reset behind the driver's back gets them back at the next read,
  select or Auto entry. The frame that returns a `ReadChannel()` result is such a Manual word.
- `ApplyConfig()` on a running driver writes every register, whatever the driver's copy says.
- A setter or `ApplyConfig()` that fails with `Bus` (or a `Verify::ReadBack` that does not confirm the write) leaves
  `GetConfig()` at the values last written successfully and `IsConfigUncertain()` true, until `ApplyConfig()` or
  `EnsureInitialized()` writes every register again.
- Calls send only the frames the datasheet needs; the table under [Timing](#timing) gives each count. Extra
  verification is chosen per call with `Verify::ReadBack` and adds the frames stated there.
- A failed frame inside a program sequence (the Auto-1 mask, an alarm group) is followed by a frame that ends the
  sequence, so the chip never takes the next command as data; `EnsureInitialized()` also opens with two such frames.
- The driver never retries a call. Retries are the caller's choice ([API guide](api-guide.md#use-it-from-a-handler)).
- A missing `DelayUs()` is a compile error; a missing `Log()` logs nothing. A misdeclared hook is a compile error.

## The identity check

The ADS795x has no ID register. `EnsureInitialized()` checks behaviour the datasheet guarantees instead, in five
frames: a Manual select of channel `a`, a "continue" word, a Manual select of channel `b` and two "continue" words.
The replies of frames 3, 4 and 5 must carry addresses `a`, `a` and `b` (the result of a select comes back two frames
later and the chip keeps converting the selected channel, Figure 51).

| Variant | `a`, `b` | In binary |
| --- | --- | --- |
| ADS7950 (4 channels) | 2, 1 | 10, 01 |
| ADS7951 (8 channels) | 5, 2 | 101, 010 |
| ADS7952, ADS7953 | 10, 5 | 1010, 0101 |

The two addresses differ in every bit the part uses, so none of these can pass:

| Fault | What the driver sees | Error |
| --- | --- | --- |
| No chip, MISO pulled high | Every reply 0xFFFF | `NoDevice` |
| MISO stuck low or floating low | Every reply 0x0000: address 0 | `DeviceIdMismatch` |
| MISO carrying a fixed word (for example 0x0E83) | One address twice | `DeviceIdMismatch` |
| SDI not reaching the chip (sections 8.4.1, 8.4.7) | The chip stays on channel 0: address 0 | `DeviceIdMismatch` |
| SDI not reaching a chip left scanning an Auto sequence | Addresses that never repeat: a, b alone would pass if the sequence were {a, b} (fuzzer) | `DeviceIdMismatch` |
| MOSI echoed onto MISO | 0xFF in the upper byte: address 15 | `DeviceIdMismatch` |

The check cannot tell the number of channels, so `Config::variant` is trusted. It reads no conversion result: the
first conversion after the reset is invalid (section 7.6), and the check does not depend on the analog front end.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port; `std::span` |
| CMake | 3.16 or later | Generates `ads7952_version.h` |
| Your port | | The bus, within the bus requirements, with CS high before the first frame; a delay; and optionally logging ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every
call on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several chips share a
bus. Nothing may run in an ISR: every call
blocks for its frames. Separate objects share no state.

The driver keeps track of the chip's mode and pipeline. Any traffic it does not send (a DMA stream of "continue"
words, another driver on the same chip select) leaves its view behind: a following read finds out from the reply
addresses and enters the mode again.

## Timing

One frame is 16 SCLK cycles with CS low, plus the CS high time; at 4 MHz about 4 µs on the wire.

| Call | Frames |
| --- | --- |
| `EnsureInitialized()` | 13, plus 3 to 10 for each alarm group not at its reset value, plus 3 with a `Config::monitor`; one `DelayUs(kPowerUpUs)` |
| `CheckScale()` | as `ReadChannel()` of the monitor channel: 1 to 3 |
| `CheckScaleCode()`, `ToMicrovolts()` | 0 |
| `ReadChannel(ch)` | 1 when `ch` is the selected Manual channel, 2 right after `SetRange()`, 3 otherwise |
| `ReadChannels(list)` | list length + 2 |
| `ReadAuto1()`, `ReadAuto2()` with k channels | k while the sequence runs; k + 1 right after `SetRange()` or a new sequence; k + 2 when the mode is entered |
| `TraceFrames(ch, words)` | the number of words |
| `SetRange()` | 1 |
| `SetAuto1Channels()` | 2; `Verify::ReadBack` adds one scan (k + 1 or k + 2) |
| `SetAuto2LastChannel()` | 1; `Verify::ReadBack` adds one scan |
| `SetAlarmThresholds()` | 4 |
| `SetGpioOutputs()` | 2; 3 with `Verify::ReadBack` |
| `ReadGpio()` | 2 |
| `ApplyConfig()` running | 1 per changed program register (2 for the Auto-1 mask), 3 to 10 per changed alarm group, 1 for a new range, 2 for new GPIO levels |
| `Deinitialize()` | 2 |

Beyond the datasheet's own sequences the driver adds only: the frame after `SetGpioOutputs()`'s mode word (the pins
change on that frame's CS falling edge, so the call returns with them changed), the frame after the alarm exit bit
(the chip leaves the sequence in it), the two frames that open `EnsureInitialized()`, and repair frames after a
failed transfer. No conversion call reads anything twice. The scale check runs only at start-up and when you call
`CheckScale()`; nothing on the conversion path runs it.

## Errors

`ads7952::Error` inside `ads7952::Result<T>`. Values 0 to 9 are the shared base set; this driver returns six of
them, plus six chip-specific values.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran since |
| `InvalidArgument` | 2 | A `Config` field fails `Validate()`, a channel the variant lacks, an empty or too long list, a threshold above 4095, a GPIO level on a pin that is not an output, a `Verify` that is not an enumerator, or `ApplyConfig()` changing the variant or package of a running driver; nothing was sent |
| `Bus` | 3 | A transfer failed, or the port could not bring the bus up |
| `Timeout` | 4 | Never returned: no call waits for the chip |
| `DeviceIdMismatch` | 5 | At start-up, the replies did not carry the selected addresses |
| `Crc` | 6 | Never returned: the chip sends no CRC |
| `Busy` | 7 | Never returned: the chip takes a frame at any time |
| `DeviceFault` | 8 | Never returned: the chip reports no faults |
| `Unsupported` | 9 | A GPIO setting the VQFN package lacks, or `SetRange()` while GPIO2 is the range input |
| `NoDevice` | 16 | At start-up, every identity reply was 0xFFFF |
| `ChannelMismatch` | 17 | A Manual reply carried another address than the channel read |
| `SequenceMismatch` | 18 | An Auto-1 or Auto-2 pass was not one turn of the programmed sequence |
| `WriteMismatch` | 19 | `Verify::ReadBack` found the GPIO levels or the scanned sequence different from what was written |
| `RangeMismatch` | 20 | The scale monitor read what the other range gives (`EnsureInitialized()`, `CheckScale()`, `CheckScaleCode()`) |
| `ScaleMismatch` | 21 | The scale monitor read outside its window in both ranges |
| `DeviceReset` | 22 | A reply from channel 0 out of turn, or Range 1 at the monitor where the driver sends Range 2: the chip was reset |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object holds a reference to the port, a `Config` (about 80 bytes: the alarm thresholds are most of it), the
pipeline state and the last reply. Stack use is small and fixed; a `Bank` is 34 bytes.

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port
in [`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

| Rule | Deviation | Why |
| --- | --- | --- |
| A chip-ID check at start-up | Replaced by the address-echo check above | The chip has no ID register |
| A reset at start-up | A software reset through the GPIO program register (DI09) | The chip has no reset pin |
| `Config{}` equals the chip's reset state | It does for the registers; `vref_mv` and `va_min_mv` describe the board | The reference and supply are board facts the driver needs to convert codes and check Range 2 |
| A `Config` field for every register field | DI04 (GPIO readout) and DI05 (power-down) are not `Config` fields | The driver keeps DI04 = 0 so every reply carries its address (it sets DI04 only inside `ReadGpio()` and `Verify::ReadBack`), and powers down only in `Deinitialize()` |
