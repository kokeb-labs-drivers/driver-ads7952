# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the ADS7952

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the ADS7952: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## What the ADS7952 does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the ADS7952: Manual mode (supported): Converts the channel each frame names; the
    result comes two frames later, tagged with its channel; Auto-1 sequence (supported): Scans a
    programmed list of channels in ascending order, one per frame; Auto-2 sequence (supported): Scans
    from channel 0 up to a programmed last channel, one per frame; Input range (supported): Selects 0 to
    VREF or 0 to 2 x VREF, and converts codes to microvolts; Alarm thresholds (supported): A high and a
    low threshold per channel, signalled on GPIO0 and GPIO1; GPIOs (supported): Four pins on TSSOP, one
    on VQFN, each an input or an output, read and written with the frames; Power-down (supported):
    Powers the chip down at the end of a frame, by command or by the GPIO3 input; Reset and checks
    (supported): Resets every register by command, waits the power-up time and checks that the chip
    answers.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## ADS7952: Typical application

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the ADS7952 typical application: the chip with U2 MCU, U3 REF5025, C1 capacitor 10 µF,
    C2 capacitor 1 µF, C3 capacitor 1 µF, C4 capacitor 150 pF, C5 capacitor 150 pF; the MCU wires carry
    the port hooks Transfer().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## ADS7952: SPI frame

<!-- figure: chip-frame-spi -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-spi.dark.svg">
  <img src="chip-frame-spi.svg"
    alt="SPI frame of the ADS7952, 16 bits, most significant bit first. Manual: mode (bits 15 to 12), program
    (bit 11), channel (bits 10 to 7), range (bit 6), power down (bit 5), GPIO read (bit 4), GPIO data
    (bits 3 to 0); Reply: channel address (bits 15 to 12), 12-bit result (bits 11 to 0). Below, the SPI
    mode 0 waveform of one transfer in the same bit grid.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## ADS7952

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the ADS7952, a 12-channel, 12-bit SAR analog-to-digital converter that samples one
    single-ended input per 16-bit SPI frame at up to 1 MSPS, in a manual order or in one of two
    automatic sequences. A MCU talks to it over SPI; it drives GPIO0 to GPIO3; it reads back Analog
    inputs, External reference. Headline figures: 12 x 12-bit single-ended inputs, one ADC; 1 MSPS
    sample rate; 20 MHz SPI clock, one 16-bit frame per sample; 2.7 to 5.25 V analog supply; I/O 1.7 to
    5.25 V; 0 to 2 x VREF input range, or 0 to VREF; 1 µA power-down current.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## ADS7952 pinout, TSSOP-38

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the ADS7952 in TSSOP-38, top view: 38 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## Wiring: Typical application

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the ADS7952 for the Typical application: MCU SPI SCK to SCLK (Transfer()); MCU SPI MOSI to
    SDI (Transfer()); MCU SPI MISO to SDO (Transfer()); MCU GPIO / CS to CS (Transfer()); REFP to
    REF5025 OUT. Supplies, grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the ADS7952 core, which sends register reads and writes
    through the SPI port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/ads7952.hpp`](../../inc/ads7952.hpp)
- `SpiInterface` in [`inc/ads7952_spi_interface.hpp`](../../inc/ads7952_spi_interface.hpp)

## A result comes back two frames after its channel

<!-- figure: frame-pipeline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="frame-pipeline.dark.svg">
  <img src="frame-pipeline.svg"
    alt="Two timing grids of 16-bit frames. Manual: a Manual word selects channel 4 in frame N, the
    multiplexer acquires it in frame N + 1 and its result comes back in frame N + 2. Auto-1: the Auto-1
    word restarts the sequence 2, 5, 6 in frame N, and from frame N + 2 on every Auto-1 word returns the
    next channel.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/frame-pipeline.py`](../../docs/diagrams/figures/frame-pipeline.py)

Drawn from:

- `select()` in [`src/ads7952.ipp`](../../src/ads7952.ipp)
- `selectChecked()` in [`src/ads7952.ipp`](../../src/ads7952.ipp)
- `readSelected()` in [`src/ads7952.ipp`](../../src/ads7952.ipp)
- `scan()` in [`src/ads7952.ipp`](../../src/ads7952.ipp)
- `kContinue` in [`inc/ads7952_registers.hpp`](../../inc/ads7952_registers.hpp)

## One read through the layers

<!-- figure: one-read-through-the-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="one-read-through-the-layers.dark.svg">
  <img src="one-read-through-the-layers.svg"
    alt="A channel read through the layers: the first ReadChannel sends a select word and two continue words
    through your port to get the result, and a repeat read of the same channel needs one continue word.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/one-read-through-the-layers.seq.yml`](../model/one-read-through-the-layers.seq.yml)

Drawn from:

- [`docs/model/one-read-through-the-layers.seq.yml`](../model/one-read-through-the-layers.seq.yml)

## SPI timing of one frame

<!-- figure: spi-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="spi-timing.dark.svg">
  <img src="spi-timing.svg"
    alt="SPI timing of the ADS7952 in mode 0: CS low frames a 16-clock word, SDI is read on each rising SCLK
    edge, SDO changes on each falling edge and is released after the 16th, with the setup, hold, delay
    and quiet times of the datasheet.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/spi-timing.timing.yml`](../model/spi-timing.timing.yml)

Drawn from:

- [`docs/model/spi-timing.timing.yml`](../model/spi-timing.timing.yml)
