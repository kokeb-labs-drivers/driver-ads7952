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
