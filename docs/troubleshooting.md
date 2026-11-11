# Troubleshooting

Symptoms, causes and fixes, cheapest check first. `GetLastReply()` holds the last reply word the driver received;
log it with the error. `TraceFrames()` shows the raw replies of a manual select, the bench's first tool.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - Power-up and the programming sequence: 8.4.3 Device Power-Up Sequence, Figure 49, [p. 30](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=30)
    - Manual mode and the two-frame read latency: 8.4.4 Operating in Manual Mode, Figures 50 and 51, Table 1, [p. 31](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=31)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns Bus

The port reported a failure: either `EnsureInitialized()` returned `false` or a transfer did.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. Check the bus pins in your port against the board.
3. With a logic analyser, check that the first frames (0x1000, 0x0000, then the reset word 0x4200) appear on the bus,
   each with its own CS pulse.

## EnsureInitialized returns NoDevice

Every identity reply read 0xFFFF: nothing drives MISO. The board is unplugged or unpowered, CS does not reach the
chip, or SDO is open with a pull-up. Check +VA and +VBD, CS and SDO continuity.

## EnsureInitialized returns DeviceIdMismatch

The replies did not carry the channel addresses the driver selected. Look at `GetLastReply()`, or run the identity
by hand with `TraceFrames()` after a temporary start:

| Replies | Likely cause | Fix |
| --- | --- | --- |
| 0x0000 every time | MISO stuck or floating low | Check SDO continuity and what else drives MISO |
| Address 0 every time, codes that vary | The chip never receives SDI: it stays in Manual mode on channel 0, its reset state, and all-zero SDI words continue it (sections 8.4.1, 8.4.7) | Check SDI (MOSI) continuity, the SPI mode and +VBD |
| One fixed word (for example 0x0E83) | MISO carries a pattern, not the chip | Unpowered board, or another device on MISO |
| 0xFFxx | MOSI echoed on MISO | SDI shorted to SDO |
| Addresses one frame late or early | CS does not toggle between frames, or the port bursts frames | One transfer per frame, CS high between them |
| Addresses with a shifted bit | Wrong SPI mode | Mode 0 |

## Reads or scans return DeviceReset

A reply came from channel 0, the channel the chip powers up on (section 8.4.3), where the driver expected another: the
chip was reset behind the driver's back, most often by a supply dip under a large load. Its registers are back at
their reset values: Range 1, no GPIO function (the alarm outputs are off), the reset alarm thresholds and sequences.
The driver is uninitialised and every call returns `NotInitialized`: call `EnsureInitialized()`, which resets and
programs the chip again and checks it. If it recurs, check +VA and its decoupling under load (section 10).

A reply from channel 0 has two other causes that look the same, so the restart also checks the bus:

- **MISO stuck low** after the start: every reply reads 0x0000, channel 0. `EnsureInitialized()` then returns
  `DeviceIdMismatch`; check the MISO line and the chip's supply.
- **A lost select from channel 0**: the chip stays on channel 0 when a select of another channel does not reach it
  (noise on SDI or CS). It is rare; `EnsureInitialized()` succeeds and the driver goes on.

## Reads return ChannelMismatch

A Manual reply carried another address than the channel read: a lost frame, noise on the bus, or a chip that stopped
following the selects (SDI lost after the start). The driver is uninitialised (it cannot rule out a reset): call
`EnsureInitialized()`. If the start fails with `DeviceIdMismatch`, check SDI.

## Scans return SequenceMismatch

An Auto-1 or Auto-2 pass was not one turn of the sequence: a frame was lost or the chip left the mode. The driver is
uninitialised: call `EnsureInitialized()`, which writes the sequence again. Another master sending frames to the chip
(a DMA stream with the wrong word) shows here too.

## Codes are all about the same, or twice what they should be

- **All codes equal, `VREF_MON` included**: the chip is not converting the channels you think. Run `TraceFrames()`
  and check the address nibble follows the select.
- **Codes doubled, a 2.5 V reference reading 4095**: the chip runs Range 1 where you expect Range 2. A reset behind
  the driver's back does this (a brown-out); the next read or Auto entry sends the range again, and a restart
  writes everything. While the driver reads channel 0 alone, the reset shows in no reply: see
  [Status and limitations](limitations.md#limits-of-the-design). A GPIO2 left as the range input ignores DI06:
  `EnsureInitialized()` resets it. With `Config::monitor` declared this is `RangeMismatch`, not a wrong value.
- **Codes halved**: the opposite, Range 2 where you expect Range 1.
- **The first channel of a scan reads mid-scale**: a reply taken from the frame before the result. The driver never
  returns those; check code that decodes raw frames by position instead of by address.

## SetGpioOutputs returns WriteMismatch

With `Verify::ReadBack`, a pin did not read back the level written. The board holds the pin (a short, a strong
pull, another driver on the net), or the pin is not an output in `Config::gpio.output_mask`.

## RangeMismatch or ScaleMismatch

The scale monitor (`Config::monitor`) is out of its window. `RangeMismatch`: the code is what the other range gives;
the chip converts in the other range. With GPIO2 as the range input, the pin is at the other level from
`Config::range`; otherwise the chip was reset after the range was sent (`CheckScale()` then returns `DeviceReset`).
After a failed `CheckScale()` the driver is uninitialised. `ScaleMismatch`: neither range explains the
code; REFP is missing or still settling after power-up, or the monitor channel does not carry the declared voltage.
Read the channel with `TraceFrames()` to see its code. See [Reference, range and supplies](reference-and-supplies.md).

## SetRange returns Unsupported

`Config::gpio.gpio2_range_input` is set: the GPIO2 pin selects the range and the chip ignores DI06. Drive the pin, or
clear the option.

## EnsureInitialized returns Unsupported

The `Config` asks for GPIO1, GPIO2 or GPIO3 on the VQFN package, which brings out GPIO0 only.
