---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# ADS7952 Driver

<p>A C++20 driver for the TI ADS7950, ADS7951, ADS7952 and ADS7953 12-bit SAR ADCs. It checks that the chip
really answers, reads channels one at a time or as a scan with the fewest frames the chip allows, and keys every
result on the channel address in its reply, on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-ads7952){ .md-button }
</div>

The driver has no platform code. You give it a small class that brings the bus up and moves one 16-bit frame at a
time; it does the rest: the reset, the identity check, the frame pipeline, the program sequences and the checks.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">12-bit SAR ADC</span>
<span class="kb-chip__name">ADS7952</span>
<span class="kb-chip__vendor">Texas Instruments</span>
</div>

Function
:   12 single-ended channels (4, 8 or 16 on the ADS7950, ADS7951, ADS7953), 12 bits, up to 1 MSPS

Interface
:   SPI mode 0, up to 20 MHz; one 16-bit frame per conversion, the result two frames after its channel

Modes
:   Manual (any channel per frame), Auto-1 (a programmed list), Auto-2 (channel 0 to a last channel)

Ranges
:   0 to VREF or 0 to 2 x VREF, with an external 2.0 V to 3.0 V reference

Extras
:   Two alarm thresholds per channel on GPIO0 and GPIO1; four GPIOs (TSSOP); power-down

Supply
:   +VA 2.7 V to 5.25 V, +VBD 1.7 V to +VA
</div>

## What you get

- **One object per chip.** `ADS7952<YourPort>` takes a reference to your port and does no I/O until
  `EnsureInitialized()`. See [API guide](api-guide.md).
- **A start-up check a dead bus cannot pass.** Two channels selected, their addresses read back two frames later:
  MISO stuck, a fixed pattern or an SDI that never reaches the chip all fail. See [Contract](contract.md).
- **The minimum traffic.** A running Auto-1 scan costs one frame per channel; a repeated channel one frame. Extra
  read-backs are chosen per call. See [Contract](contract.md#timing).
- **Errors you can act on.** Every call returns `Result<T>` with an `Error` such as `Bus`, `ChannelMismatch` or
  `SequenceMismatch`. See [API guide](api-guide.md#handle-errors).
- **Zero-overhead portability.** The port is a CRTP class checked by a concept: a transfer and a delay, plus an
  optional logging hook. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** The host tests build with `-fno-exceptions -fno-rtti`. See
  [Contract](contract.md).
- **Tested without hardware.** Host tests run the driver against a protocol model of the chip with the faults seen
  on hardware, and a fuzzer attacks every reply. See [Testing and CI](testing.md) and
  [Status and limitations](limitations.md).

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: ADS79xx 12-, 10-, 8-Bit, 1-MSPS, 16-, 12-, 8-, 4-Channel, Single-Ended, Serial Interface ADCs (Texas Instruments SLAS605) | C, 2018-07 | [PDF](https://www.ti.com/lit/ds/symlink/ads7952.pdf) · [vendor page](https://www.ti.com/product/ADS7952) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the chip, write the port, read a channel in about 15 lines.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    Pins, the frame, the modes, the registers and power.

-   :material-file-document-check-outline: **[Contract](contract.md)**

    ---

    What the driver promises, what it needs, threading and errors.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Start, read, scan, set ranges, alarms and GPIOs, and handle errors, task by task.

-   :material-swap-horizontal: **[Porting](porting.md)**

    ---

    Implement the port for your MCU.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
