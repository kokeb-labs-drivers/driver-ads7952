# API guide

How to use each part of the ADS7952 driver: start it, read channels one at a time or as a scan, change the range, the
sequences, the alarms and the GPIOs, and handle every error. Every snippet compiles in the host build.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - Power-down through DI05: 8.3.2 Power Saving, [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29)
    - Manual mode and the two-frame read latency: 8.4.4 Operating in Manual Mode, Figures 50 and 51, Table 1, [p. 31](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=31)
    - Auto-1 mode and its program register: 8.4.5 Operating in Auto-1 Mode, Figures 52 to 54, Tables 2 to 4, [p. 34](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=34)
    - Auto-2 mode and its program register: 8.4.6 Operating in Auto-2 Mode, Figures 55 to 57, Tables 5 and 6, [p. 38](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=38)
    - Output codes and the LSB size: 8.5.1 Digital Output, Table 8, [p. 41](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=41)
    - GPIO program register and the register reset: 8.5.2 GPIO Registers, Figure 58, Table 11, [p. 42](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=42)
    - Alarm thresholds: 8.5.3 Alarm Thresholds for GPIO Pins, Figure 59, Tables 12 and 13, [p. 43](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=43)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

| Group | Calls |
| --- | --- |
| Lifecycle | `EnsureInitialized()`, `IsInitialized()`, `Deinitialize()`, `ApplyConfig()`, `GetConfig()` |
| Conversions | `ReadChannel()`, `ReadChannels()`, `ReadAuto1()`, `ReadAuto2()`, `TraceFrames()` |
| Settings | `SetRange()`, `SetAuto1Channels()`, `SetAuto2LastChannel()`, `SetAlarmThresholds()`, `SetGpioOutputs()`, `ReadGpio()` |
| No I/O | `GetChannelCount()`, `ToMicrovolts()`, `GetLastReply()`; free: `Validate()`, `ResetConfig()`, `CodeToMicrovolts()`, `ChannelCount()`, `reg::ReplyAddress()`, `reg::ReplyCode()` |

Every call that can fail returns `ads7952::Result<T>`: check `ok()` (or the `bool` conversion), then read `value`.
The [Contract](contract.md#timing) page gives the frames each call sends.

## Start

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

`EnsureInitialized()` is the one way to start. Its first call checks the configuration, brings the bus up, resets
the chip through the GPIO program register, writes every program register and the range, and checks that the replies
carry the channel addresses it selected ([Contract](contract.md#the-identity-check)). After it succeeds, later calls
return `Ok` with no I/O; after it fails, the next call starts again from the reset. `Deinitialize()` powers the chip
down; `Deinitialize()` then `EnsureInitialized()` is the full restart.

## Read one channel or a list

`ReadChannel(ch)` converts one channel in Manual mode. A new channel costs three frames (select, acquire, return); the
same channel again costs one, because the chip keeps converting the channel it was given.

```cpp
--8<-- "tests/docs/api_tasks.cpp:manual"
```

`ReadChannels(list, codes)` pipelines the selects as the datasheet's Figure 51 does: list length + 2 frames for the
whole list, against three per channel with `ReadChannel()`. Repeats are allowed. Both calls check the address of every
reply and return `ChannelMismatch` instead of a result for another channel, or `DeviceReset` when the reply came from
channel 0, the channel a reset puts the chip on; after either the driver must be started again.

## Scan a sequence: the hot path

```cpp
--8<-- "tests/docs/api_tasks.cpp:scan"
```

`ReadAuto1()` returns one pass of the Auto-1 sequence (`Config::auto1_channels`) in a `Bank`: `code[ch]` for every
`ch` in `mask`. While the chip runs the sequence, a pass of k channels is k frames, the minimum the chip allows:
each is the Auto-1 word with DI11 = 1 and no restart, which re-sends the range (a "continue" word would not). The
first call, or the first after a manual read, enters Auto-1 with one more frame and skips the reply in flight
(k + 2). The replies must be one turn of the sequence in the chip's order, starting anywhere: anything else
is `SequenceMismatch` (`DeviceReset` for a reply from channel 0 out of turn), and the driver must be started
again. `ReadAuto2()` does the same for channel 0 to `Config::auto2_last_channel`.

```cpp
--8<-- "tests/docs/api_tasks.cpp:sequence"
```

`SetAuto1Channels(mask)` writes the mask (two frames). Call it when the list changes, not before every pass: the
chip keeps it until a reset. With `Verify::ReadBack` it scans once and checks the addresses are the new sequence
(`WriteMismatch` otherwise), which also leaves the sequence running for the next `ReadAuto1()`.

A DMA stream that sends "continue" words by itself can take over once the sequence runs: decode each word with
`reg::ReplyAddress()` and `reg::ReplyCode()`, and key results on the address. When the stream stops, the next
`ReadAuto1()` checks the replies and enters the mode again only if the chip left it.

## Range and conversion to volts

`SetRange(range)` sends one mode word with the new DI06. Results from the second frame after it use the new range;
the next conversion call skips the one in between by itself. Range 2 (0 to 2 x VREF) is only allowed while
2 x `vref_mv` <= `va_min_mv` (section 7.6). With GPIO2 as the range input, the pin selects the range and `SetRange()`
returns `Unsupported`.

`ToMicrovolts(code)` converts with the configured range and reference: one LSB is full scale / 4096 (Table 8).
`ToMicrovolts(bank, channel)` converts with `Bank::range`, the range the scan was made in, so a bank kept across
`SetRange()` still converts right. `CodeToMicrovolts(code, range, vref_mv)` does the same without a driver object.

## Proving the scale

The chip cannot report its range, and a lost range doubles or halves every code while every address still checks
out. Declare a channel that carries a voltage derived from REFP in `Config::monitor`: `EnsureInitialized()` checks it
once (3 frames), `CheckScale()` checks it on demand (1 to 3 frames), and `CheckScaleCode(code)` judges a code you
already have, such as the monitor's entry of each bank, with no frame at all. They return `RangeMismatch` when the
code is what the other range gives and `ScaleMismatch` when neither range explains it. How to wire the monitor, choose
the range for a supply and decode ratiometric sensors: [Reference, range and supplies](reference-and-supplies.md).

## Alarms

```cpp
--8<-- "tests/docs/api_tasks.cpp:alarm"
```

`SetAlarmThresholds(ch, {low, high})` writes both thresholds of one channel in four frames. The chip keeps the upper
10 bits of each and compares them with the upper 10 bits of every result of that channel, in every mode. The alarm
reaches a pin only when `Config::gpio.alarm_output` routes it there. Thresholds cannot be read back; set them all
at once in `Config::alarms` and `EnsureInitialized()` writes every group that differs from the reset value.

## GPIOs

```cpp
--8<-- "tests/docs/api_tasks.cpp:gpio"
```

`Config::gpio` sets each pin's direction and function. `SetGpioOutputs(levels)` drives the output pins: a mode word
with the new levels and the frame on whose CS falling edge the chip drives them, so the pins have changed when it
returns. `Verify::ReadBack` adds one frame and reads the levels back from the pins (`WriteMismatch` when a pin does
not follow, for example one held by the board). `ReadGpio()` reads all four levels in two frames.

## Bring-up trace

```cpp
--8<-- "tests/docs/ports.cpp:trace"
```

`TraceFrames(ch, words)` selects a channel and returns every reply word unparsed. From `words[2]` on, the address nibble
must be the channel; a nibble that never follows means SDI does not reach the chip.

For bring-up and fault finding the driver keeps what it saw and offers one raw access (handbook KB-CPP-129):

| Call | I/O | What it gives |
| --- | --- | --- |
| `TraceFrames(ch, words)` | `words.size()` frames, one shot, no retry, no check of the replies | Every reply word unparsed. Its one side effect: the chip is left in Manual mode on `ch`, as after `ReadChannel(ch)` |
| `GetLastReply()` | none | The last reply word of any call, the driver's own frames included; 0xFFFF before the first and after a failed transfer |

There is no status latch (handbook KB-CPP-119) because the chip has no clear-on-read or write-1-to-clear flag: a
reply carries only the channel address (or the GPIO levels) and the code, and the alarm outputs are pins that the
chip sets on the 12th SCLK falling edge of a frame and clears on the 10th of the next (section 8.1), with nothing
to read back.

## Use it from a handler

On a board, the driver sits under a handler that owns it, the port and the bus. The driver leaves to the handler
what it cannot know:

- **When to probe.** Never from a static constructor or at boot: an unplugged board leaves MISO floating, and the
  bus is shared. Construct the driver at boot if you like (it does no I/O), call `EnsureInitialized()` from the
  attach path, and on each attach start again with `Deinitialize()` then `EnsureInitialized()`.
- **The shared bus.** The driver takes no lock and never configures the bus. Take the bus lock around each call and
  pause other traffic on the bus for its duration.
- **The scan.** Write the channel list once (`SetAuto1Channels()`, or `Config::auto1_channels` before the start),
  then call `ReadAuto1()` every tick. Do not write the mask before every pass: two frames per pass for nothing.
- **Board-level checks.** The driver proves the address of every result. Declare `VREF_MON` as `Config::monitor`
  and judge it in every bank with `CheckScaleCode()`: that catches a lost range word and a reset during a running
  sequence at no frame cost. Check also for a bank whose counts are all identical; it catches a broken front
  end (with SDI not reaching the chip, every count once read 0x0E83).
- **Retries.** The driver never retries. What to do on each error:

| Error | In the start | In a read or a scan | In a setting |
| --- | --- | --- | --- |
| `NoDevice` | Board absent or unpowered: report it, probe again on the next attach | | |
| `DeviceIdMismatch` | SDI, MISO or CS broken, or another part: report, do not use | | |
| `ChannelMismatch` | | `EnsureInitialized()` (the driver is uninitialised) | |
| `SequenceMismatch` | | `EnsureInitialized()` (the driver is uninitialised) | |
| `DeviceReset` | | The chip was reset: `EnsureInitialized()` re-arms alarms and GPIOs | |
| `WriteMismatch` | | | GPIO: a pin held by the board. Sequence: restart |
| `Bus` | The port failed: check the bus, then probe again | Same | Same; the chip may hold part of the setting (`IsConfigUncertain()`): `ApplyConfig(GetConfig())` |

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Signal | Meaning | What to do |
| --- | --- | --- |
| `Error::NoDevice` | At start-up every reply was 0xFFFF: nothing drives MISO | Board plugged and powered? CS and SDO wiring |
| `Error::DeviceIdMismatch` | At start-up the replies did not follow the selects | Address 0 every time: SDI not reaching the chip, or MISO stuck low. Run `TraceFrames()` |
| `Error::DeviceReset` | A reply came from channel 0 out of turn, or the monitor reads Range 1: the chip was reset | The driver is uninitialised; `EnsureInitialized()` programs the chip again. Check +VA under load |
| `Error::ChannelMismatch` | A reply carried another channel's address | A lost frame: the driver is uninitialised; `EnsureInitialized()` |
| `Error::SequenceMismatch` | An Auto pass was not one turn of the sequence | The chip left the mode or lost its mask: `EnsureInitialized()` |
| `Error::WriteMismatch` | A read-back differed | A GPIO pin held by the board, or a program frame lost |
| `Error::RangeMismatch` | The scale monitor reads what the other range gives | Do not publish the bank. Check the GPIO2 strap; restart, which resets the chip and checks again |
| `Error::ScaleMismatch` | The scale monitor fits neither range | Do not publish. REFP missing or not settled, or the monitor channel not wired as declared |
| `Error::Unsupported` | A GPIO the VQFN lacks, or `SetRange()` with GPIO2 as range input | Fix the `Config` |
| `Error::Bus` | The port failed a transfer or the bus bring-up | Check the port and the wiring |
| `Error::InvalidArgument` | A `Config` field or an argument is out of range; nothing was sent | Fix the value (`Validate()`, `GetChannelCount()`) |
| `Error::NotInitialized` | `EnsureInitialized()` has not succeeded | Call it |

```cpp
--8<-- "tests/docs/api_tasks.cpp:recover"
```

`Error::Timeout`, `Error::Crc`, `Error::Busy` and `Error::DeviceFault` belong to the shared base set but are never
returned by this driver: no call waits for the chip, which sends no CRC, is never busy and reports no fault. Every
value, with the calls that return it, is on the [Contract](contract.md#errors) page. `ads7952::ToString()` names an
error for a log.

Next: [Configuration](configuration.md).
