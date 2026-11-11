# Reference, range and supplies

How a code becomes a voltage, which voltages set that scale, how to choose the range for a board, how the driver
proves the scale at run time, and how to power and lay out the chip. Read this before you design a board or write
the code that turns codes into engineering units: a wrong scale passes every address check the driver makes.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - Absolute limits of the inputs and supplies: 7.1 Absolute Maximum Ratings, [p. 10](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=10)
    - Supplies, reference and SCLK limits: 7.3 Recommended Operating Conditions, [p. 10](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=10)
    - Input ranges, power-up time and the invalid first conversion: 7.6 Electrical Characteristics (ADS7950 to ADS7953), [p. 12](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=12)
    - The external reference and its 10 µF capacitor: 8.3.1 Reference, [p. 29](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=29)
    - Output codes and the LSB size: 8.5.1 Digital Output, Table 8, [p. 41](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=41)
    - GPIO program register and the register reset: 8.5.2 GPIO Registers, Figure 58, Table 11, [p. 42](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=42)
    - Analog input, source impedance and MXO to AINP: 9.1.1 Analog Input, Figures 60 and 61, [p. 46](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=46)
    - Supply range, decoupling and +VA at or above +VBD: 10 Power Supply Recommendations, [p. 51](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=51)
    - Layout of grounds, supply and reference capacitors: 11.1 Layout Guidelines; 11.2 Layout Examples, Figures 69 and 70, [p. 52](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=52)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Three voltages, three jobs

| Voltage | Pin | What it sets | `Config` field |
| --- | --- | --- | --- |
| VREF | REFP (to REFM) | The scale of every code: full scale is VREF in Range 1, 2 x VREF in Range 2 | `vref_mv` |
| +VA | +VA | The analog supply. It limits the inputs (never above +VA + 0.3 V) and decides whether Range 2 is specified | `va_min_mv` |
| +VBD | +VBD | The SPI and GPIO logic levels, 1.7 V to +VA | none |

A fourth voltage belongs to your sensors, not to the chip: the **excitation** of a ratiometric sensor, whose output is
a fraction of its own supply. It is often the +VA rail, but it is never the ADC's full scale.
[Ratiometric sensors](#ratiometric-sensors) shows how to decode against it.

## From a code to a voltage

The result is 12 bits, straight binary, and one LSB is full scale / 4096 (Table 8):

| Range | Full scale | One LSB (VREF = 2.5 V) | Code 2048 | Code 4095 |
| --- | --- | --- | --- | --- |
| 1 (`Range::Vref`) | VREF | 610.4 µV | VREF / 2 = 1.25 V | VREF - 1 LSB = 2.4994 V |
| 2 (`Range::TwoVref`) | 2 x VREF | 1.2207 mV | VREF = 2.50 V | 2 x VREF - 1 LSB = 4.9988 V |

Divide by **4096**, not 4095: dividing by 4095 reads every voltage 0.024 % high. The driver's `ToMicrovolts()` and
`CodeToMicrovolts()` use Table 8 and return the bottom of the code's step.

The driver keeps the range with the result:

- `ToMicrovolts(code)` uses the range configured now.
- Every `Bank` carries `Bank::range`, the range its codes were converted in, and `ToMicrovolts(bank, channel)` uses
  it. A bank kept across `SetRange()` still converts correctly.
- With GPIO2 as the range input, `Config::range` is what you declare the pin to be; the driver cannot see the pin.
  Declare a [scale monitor](#proving-the-scale-the-scale-monitor) to check the declaration.

## Choosing the range

Range 2 is specified only "while 2 x VREF <= +VA" (section 7.6): the full-scale input span and the absolute input
range of Range 2 both carry that condition. `Validate()` therefore refuses Range 2 unless
2 x `vref_mv` <= `va_min_mv`, and `va_min_mv` is the **lowest** +VA the board guarantees: the regulator's minimum
after its tolerance, ripple and load steps, not the nominal rail.

| Board | Range | Why |
| --- | --- | --- |
| Inputs up to about 2.5 V, any +VA from 2.7 V | 1, VREF 2.5 V | Twice the resolution of Range 2, no condition on +VA |
| Inputs up to 5 V, +VA guaranteed >= 5.0 V (for example a 5.1 V rail at +/-1.5 %) | 2, VREF 2.5 V | Specified; full scale 5.0 V |
| Inputs up to about 4 V, +VA a 5 V rail at +/-5 % (4.75 V minimum) | 2, VREF 2.048 V | 2 x 2.048 = 4.096 V <= 4.75 V. Full scale 4.096 V, one LSB exactly 1 mV. Footnote 6 of section 7.6: below 2.4 V the SNR is lower |
| Inputs up to 5 V, +VA a nominal 5.0 V that can sag | Change the board | Range 2 with 2 x VREF above +VA is outside the datasheet, and inputs near 5 V exceed +VA + 0.3 V when the rail sags |

Two rules hold in every range:

- **No input above +VA + 0.3 V or below -0.3 V**, at any time, powered or not (section 7.1). A sensor powered from
  another rail can drive an input while +VA is off; power them from the same rail or clamp the inputs.
- **The range does not survive a reset.** The chip powers up and resets in Range 1 (section 8.4.3, Table 1). The
  driver resets the chip in `EnsureInitialized()` and sends the range in every mode word it writes, the read frame
  of `ReadChannel()` included, but a brown-out between two calls puts the chip back in Range 1 without a flag. A
  reply from channel 0 out of turn shows it (`DeviceReset`); while the driver reads channel 0 alone only the monitor
  can, and `CheckScale()` then returns `DeviceReset`.

## Proving the scale: the scale monitor

The chip has no register to read its range or its reference back. What the driver can check for free, the channel
address of every reply, does not change with the range: a chip that lost its range returns perfectly addressed
codes that are twice (or half) what they should be. The address nibble (DO15:12, section 8.1) does not depend on
the range, and one LSB is VREF / 4096 in Range 1 against 2 x VREF / 4096 in Range 2 (Table 8): a chip back in
Range 1 reads an input below VREF at twice its code, with every reply address valid.

The only witness is a known input. Wire one channel to a voltage derived from REFP and declare it in
`Config::monitor`:

```cpp
--8<-- "tests/docs/reference.cpp:config"
```

| Monitor input | Range 1 code | Range 2 code | Notes |
| --- | --- | --- | --- |
| REFP itself | 4095 (clipped) | 2048 | Works for Range 2 only: in Range 1 it sits at full scale, where a clipped input also sits. `Validate()` refuses it in Range 1 |
| REFP / 2, two equal resistors | 2048 | 1024 | Works in both ranges. Use 0.1 % resistors of the same series |

A voltage derived from REFP converts to the same code whatever REFP's exact value, so the check does not depend on
the reference's accuracy. A monitor that is not derived from REFP also works, but its window must then absorb the
reference's tolerance too.

`Validate()` refuses a monitor that could not do its job: a channel the part lacks, a zero tolerance, a window that
touches code 0 or 4095 (a dead or clipped input would pass), or windows of the two ranges that touch.

| Where | What it checks | Frames | Result |
| --- | --- | --- | --- |
| `EnsureInitialized()` | Once, last, after the identity check | 3 | `RangeMismatch` or `ScaleMismatch`; the driver stays uninitialised |
| `CheckScale()` | On demand: the chip takes the range the driver sends, and REFP is there | 1 to 3 | The monitor's code, or the error |
| `CheckScaleCode(code)` | A code you already have: the monitor's entry of each `Bank`, or a DMA-decoded word | 0 | `Ok`, `RangeMismatch` or `ScaleMismatch` |

`RangeMismatch` means the code is what the other range gives: GPIO2 is strapped to the other level, or the chip was
reset. `ScaleMismatch` means neither range explains it: REFP is missing or not settled, or the monitor channel is
not wired as declared.

**On the hot path, check the bank, not the chip.** `CheckScale()` selects the monitor with a fresh mode word, which
carries the range again, so it cannot see a range that was lost earlier and it costs frames. Put the monitor channel in
the Auto-1 sequence and judge its code in every bank with `CheckScaleCode()`: the cost is one channel of the scan and
no extra frame.

```cpp
--8<-- "tests/docs/reference.cpp:bank"
```

After a `RangeMismatch` or `ScaleMismatch`, do not publish the bank. Restart with `Deinitialize()` and
`EnsureInitialized()`: the reset and the start-up check run again. A failed `CheckScale()` leaves the driver
uninitialised by itself.

## Ratiometric sensors

A ratiometric sensor (for example a bridge or an amplified pressure sensor rated "10 % to 90 % of supply") outputs a
fraction of **its own supply**. The ADC measures volts against VREF. Decoding the sensor against the ADC's full scale,
or against a nominal 5.0 V, is only right while the sensor's supply is exactly that value. A supply that sags to
4.45 V while the decoder assumes 5.0 V under-reads every span by 11 % (4.45 / 5.0 = 0.89).

Measure the sensor's supply on another channel of the same scan and divide:

```cpp
--8<-- "tests/docs/reference.cpp:ratiometric"
```

| Choice | Effect |
| --- | --- |
| Supply measured in the same bank | Reference and range cancel; supply ripple between the two conversions does not |
| Divider for the supply channel | Must keep the supply's channel inside the range: 1/2 puts a 5 V supply at 2.5 V |
| No supply measurement | The result is only as good as the rail's tolerance: say so where the value is used |

## Power and layout

From sections 8.3.1, 9.1.1, 10 and 11 of the datasheet:

| Item | Requirement |
| --- | --- |
| +VA | 2.7 V to 5.25 V, well regulated; 1 µF ceramic at each +VA pin, as close as possible |
| +VBD | 1.7 V to +VA (never above +VA); 1 µF ceramic at the pin |
| REFP | An external low-noise reference (the datasheet names the REF5025, a 2.5 V band-gap); 10 µF ceramic from REFP to REFM, directly next to the pins, its ground routed to REFM by a short trace |
| Characterised reference | The electrical tables are measured at VREF = 2.5 V +/- 0.1 V; section 8.3.1 asks for 2.5 V +/- 10 mV; the chip operates from 2.0 V to 3.0 V |
| Grounds | A copper fill under the chip ties AGND, BDGND, AINM and REFM, with at least four vias to the analog ground plane |
| Vias | None between a decoupling capacitor and its pin |
| Analog inputs | With MXO tied to AINP, a source impedance of 50 Ω or less; a buffer between MXO and AINP relaxes it (Figure 61) |
| Digital lines | Kept apart from the analog inputs: the input bandwidth makes the chip sensitive to digital edges |
| Inputs | -0.3 V to +VA + 0.3 V, absolute (section 7.1) |

Design guidance that is not in the datasheet, to confirm on your bench:

- A resistor divider for the monitor or a supply channel has a high source impedance. Put a capacitor from the
  channel to AGND (for example 100 nF) so that the sample charge comes from it, and check that the reading does not
  move with the SCLK rate or the position of the channel in the sequence.
- Measure +VA at the chip under full load (the sensors on, motors and other loads running) before choosing the
  range. The value to declare is the lowest you saw, less the regulator's tolerance.

## Checklist for a new board

1. Choose VREF and the range from the table in [Choosing the range](#choosing-the-range); write `vref_mv` and the
   worst-case `va_min_mv`.
2. Wire a scale monitor (REFP, or REFP / 2 for Range 1) to a spare channel and declare `Config::monitor`.
3. Put the monitor channel in the Auto-1 sequence and check its code in every bank.
4. Decode ratiometric sensors against their measured supply, never against the ADC's full scale.
5. Convert with `ToMicrovolts(bank, channel)`, so a code is always converted with the range it was made in.
6. On the bench: start with GPIO2 strapped wrong (if you use it) and with REFP disconnected; `EnsureInitialized()`
   must return `RangeMismatch` and `ScaleMismatch`.

## Worked example: a 5 V sensor board

A board with ratiometric 5 V sensors, +VA from a nominal 5 V rail and a 2.5 V reference in Range 2:

- **Check Range 2 against the lowest +VA.** Range 2 needs 2 x VREF <= +VA (section 7.6). With +VA measured at
  4.89 V to 4.91 V under load, below 2 x VREF = 5.0 V, `Validate()` refuses `va_min_mv` = 4890. Either raise and
  tighten +VA, or move to a 2.048 V reference (full scale 4.096 V; VREF may be 2 V to 3 V, section 7.3), and decide
  it with the sensor span in hand.
- **Declare the monitor channel from the schematic.** With channel 11 wired to REFP itself, the scale monitor is
  `{11, 2500000, 100000}` and reads 2048 in Range 2. A divided monitor (REFP through a resistor divider) needs the
  divided voltage instead, so read the schematic before declaring it.
- **Divide by 4096.** One LSB is full scale / 4096 (Table 8); a division by 4095 reads 0.024 % high.
- **Decode the sensors against a measured excitation**, taken from the same bank where you can
  ([Ratiometric sensors](#ratiometric-sensors)).

**Not verified on hardware.** Every number on this page is from the datasheet or is a worked example; this driver has
not run on a board yet ([Status and limitations](limitations.md)).
