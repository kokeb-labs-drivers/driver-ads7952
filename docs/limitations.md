# Status and limitations

What this driver has and has not been shown to do, so you can decide whether it is ready for your use.

## Status

| | |
| --- | --- |
| **Version** | 0.1.0, not released |
| **On hardware** | **Not verified on hardware.** No board has run this driver yet; [Before you depend on it](#before-you-depend-on-it) lists the bench steps |
| **Tested against** | A protocol model of the four parts ([Testing and CI](testing.md)), a fuzzer and the quality gates |
| **Assurance level** | `high` (Kokeb Labs handbook): not qualified for use in a safety function |
| **Parts** | ADS7950, ADS7951, ADS7952, ADS7953 (12-bit). The 10-bit ADS7954 to ADS7957 and 8-bit ADS7958 to ADS7961 share the frame format but are not supported |

## Design choices and their datasheet basis

Each choice below closes a failure that a real bus or board can produce. It cites the datasheet section it rests
on; the model has the fault, and a test pins the behaviour.

- **The identity check needs SDI to reach the chip.** After power-up or a reset the chip runs Manual mode on
  channel 0 (sections 8.4.1 and 8.4.3), and a frame whose DI15:12 are 0000 keeps the current mode and channel
  (section 8.4.7, Table 7). A chip that never receives SDI reads all zeros and so answers channel 0 frame after
  frame, with codes that look like data; a MISO stuck low reads as channel 0 too, and a MISO carrying a fixed
  pattern returns one address. The identity check selects two channels whose addresses differ in every bit and
  demands the first twice and then the second (a, a, b) ([Contract](contract.md#the-identity-check)). A chip left
  scanning an Auto-1 sequence of exactly those two channels answers a, b in turn but never repeats an address, so
  it fails as well. The model has the faults (`deaf_sdi`, `Miso::StuckLow`, `Miso::Pattern` with 0x0E83,
  `Miso::FloatHigh`, `Miso::EchoMosi`); the tests and the fuzzer cover them.
- **A result belongs to the channel in its reply, two frames after the select.** The datasheet puts a channel's
  result in frame N + 2 (section 8.4.4, Figure 51), not N + 1. The driver tracks the pipeline and checks the address
  of every result it returns (`ChannelMismatch`, `SequenceMismatch`), and of the replies a new select or an Auto
  entry discards: from a settled Manual channel they must carry that channel.
- **A reset behind the driver's back is detected where a reply can show it, and the driver then stops.** A reset
  (a brown-out) puts every register back to its reset value: Manual mode on channel 0, Range 1 (section 8.4.3,
  Table 1), every GPIO an input with no alarm function (Table 11), the alarm thresholds to all ones and all zeros
  (section 8.5.3); the first conversion after it is invalid (section 7.6). The chip has no flag for it, so its only
  sign is a reply from channel 0 where the driver expects another channel. That is `DeviceReset`. After it, and
  after any `ChannelMismatch` or `SequenceMismatch` (a lost frame and a reset cannot always be told apart), the
  driver is uninitialised: every call returns `NotInitialized` until `EnsureInitialized()` resets the chip, programs
  every register in the order of section 8.4.3 and discards the invalid first conversion. The driver never goes on
  writing from a copy of a configuration the chip has lost. This is the fail-safe choice over reprogramming on its
  own: an alarm interlock that a reset disarmed is reported to the application, which decides when to re-arm it.
- **Every mode word carries the range.** A mode word changes the range only with DI11 = 1 (Tables 1 and 2). Every
  mode word the driver sends has DI11 = 1 and the configured range, and so does the frame that returns a
  `ReadChannel()` result (the Manual word for the same channel, not a "continue" word, at the same frame count), so
  the range is back at the next read after a reset the driver did not see. The register reset at every start (DI09
  of the GPIO program register, Table 11) also clears a GPIO2 left as the range input (DI07).
- **`ApplyConfig()` writes every register.** No register can be read back (section 8.4.2), so the driver's copy is
  never the reason to skip one: `ApplyConfig(GetConfig())` writes the GPIO program register, both sequence program
  registers, every alarm threshold and the mode word (6 + 10 frames per alarm group, 36 on the ADS7952).
- **A write whose transfer failed may have taken effect.** A transfer the port reports as failed may still have
  clocked the frame into the chip. After a failed range, GPIO, sequence or alarm frame (or a read-back that did not
  confirm one), `GetConfig()` keeps the values last written successfully, never those of the failed write, and
  `IsConfigUncertain()` is true: the chip may hold either. Alarm thresholds in particular are never assumed set.
  `ApplyConfig(GetConfig())` or `EnsureInitialized()` writes every register and clears it. The range and the GPIO
  levels also go out again with every mode word, select and read frame.
- **Every pass of a sequence is checked.** A reset returns the chip to Manual mode (section 8.4.3), and a corrupted
  frame that the chip reads as a mode word changes the mode (section 8.4.2.1). A pass whose addresses are not one
  turn of the sequence is `SequenceMismatch` (or `DeviceReset` for a reply from channel 0 out of turn), and the
  driver must be started again, which programs the sequence and restarts it at its first channel (section 8.4.5).
- **Only `EnsureInitialized()` probes.** SDO is driven only while CS is low: it goes to 3-state after the 16th
  SCLK falling edge and when CS goes high (section 8.1; t<sub>d3</sub> and t<sub>d4</sub>, section 7.9). With the
  chip unpowered or unplugged, MISO floats to whatever the board pulls it to. The constructor does no I/O; call
  `EnsureInitialized()` from an explicit attach path, once the board is known to be powered.
- **One LSB is full scale / 4096.** Table 8 makes code 4095 full scale less one LSB. `CodeToMicrovolts()` divides by
  4096; a division by 4095 would read 0.024 % high.
- **The port reports every bus error.** `Transfer()` returns `bool`, and the `SpiPort` concept rejects a port whose
  transfer returns `void`: a port that cannot report a failure would turn every failed transfer into a frame of
  data.
- **The Auto-1 mask is written once.** The program register keeps its value until power-down, reset or
  reprogramming, and Auto-1 may be left and entered again without disturbing it (section 8.4.1). The driver writes
  the mask (two frames, Table 3) only when it changes, and a running sequence costs k frames per pass.
- **A program sequence is always closed.** Auto-1 programming takes two frames, and the chip takes the second
  frame's word as the mask whatever it holds (section 8.4.5, Figure 54 and Table 3). An alarm sequence stays open
  until a frame with the exit bit DI12 = 1, and every word until then is taken as a threshold (section 8.5.3,
  Table 13). After a failed frame inside a sequence the driver therefore repeats the mask or closes the alarm
  sequence, and every start-up opens with two frames that end any sequence (found by the bus-fault sweep and the
  fuzzer).

## Limits of the design

- **A reset at any moment the chip rests on channel 0 in Manual mode shows in no reply.** Channel 0 is the power-on
  channel (section 8.4.3), so its replies carry the same address before and after. That covers more than a loop of
  `ReadChannel(0)`: in any mix of reads, a reset while the chip rests on channel 0 (after a read of channel 0, or a
  `ReadChannels()` list that ends with it) is followed by a select from channel 0, which looks the same, and a
  `ReadChannels()` list or a `TraceFrames()` selects before it reads. Such a reset may never be seen. Two results
  after it are wrong: the invalid first conversion (section 7.6) and one acquired in Range 1 before the next read
  frame sent the range again. Elsewhere the one reply in flight across a reset can still be returned when the chip
  keeps it (right address, invalid code); the next reply is `DeviceReset`. An Auto-1 sequence of channel 0 alone, or
  Auto-2 ending at channel 0, returns up to three passes after a reset (the same two wrong results) before the chip,
  back in Auto-1 with the reset sequence, shows another channel. Until the configuration is written again, the alarm
  function and thresholds stay lost. Where GPIO0 or GPIO1 alarms feed an interlock, use these mitigations:
  - set `Config::park_channel` (opt-in) to a channel other than 0. After each `ReadChannel(0)` the chip then rests on
    that channel (one frame more; 4 frames per repeated `ReadChannel(0)` instead of 1), and the next read sees a
    reset as `DeviceReset`;
  - call `ApplyConfig(GetConfig())` (36 frames on the ADS7952) at the rate the interlock's reaction time needs, or
    `Deinitialize()` and `EnsureInitialized()`;
  - declare a `Config::monitor` on a channel other than 0 and call `CheckScale()` (a Range 1 reading is `DeviceReset`).

  Bench step 5 confirms the detection on hardware.
- **A frame lost before the chip, but reported as sent, is not always caught.** A lost range or GPIO word leaves the
  chip on the old setting until the next mode word; only a known input shows it, which is what `Config::monitor`
  and `CheckScaleCode()` are for. A MISO line that fails after start-up into a fixed word whose address happens to
  match the channel being read is taken as that channel's result. Both need a board-level check.
- **The datasheet does not say whether the word sent in the frame that performs a register reset is obeyed.**
  Table 11 says only that the reset happens in the next CS frame. The driver sends a "continue" word there, which
  is harmless either way, and the model ignores that frame's word. Bench step 1 below runs the start-up that
  depends on it.

## Handbook rules this chip gives no handle for

Some driver rules of the handbook need a feature the ADS795x does not have. Each is met as far as the chip allows,
and the rest is stated here:

| Rule | What the chip has | What the driver does |
| --- | --- | --- |
| Clear-on-read flags in a latch (KB-CPP-119) | No clear-on-read or write-1-to-clear flag: a reply is an address (or the GPIO levels) and a code; the alarm outputs are pins, set on the 12th SCLK falling edge of a frame and cleared on the 10th of the next (section 8.1) | No latch. `GetLastReply()` keeps the last raw reply |
| Wait for the chip's ready indication (KB-CPP-124) | No ready, busy or ID bit; only the power-up time after a reset or wake-up (section 7.6) | Waits the power-up time, discards the invalid first conversion, and treats the identity check, which the chip must answer frame by frame, as the proof that it is up |
| A stuck line's reply is never data (KB-CPP-116) | 0x0000 is also channel 0 converting 0 V, and 0xFFFF channel 15 converting full scale (ADS7953) | At start-up both fail the identity check (`DeviceIdMismatch`, `NoDevice`). After start-up a MISO stuck low reads as channel 0 at code 0: a scan of two or more channels catches it (the addresses must step), a single read of channel 0 cannot. When code 0 on channel 0 matters, confirm it: `CheckScale()` on a monitor channel other than 0 fails on a MISO stuck low (its reply address is wrong) |
| Register read-back (KB-CPP-122, KB-ARCH-041) | No register can be read back except the GPIO levels; the range is not reported (section 8.4.2) | Registers are written whole from the driver's copy; `Verify::ReadBack` reads the GPIO levels or scans the new sequence, and the scale monitor witnesses the range |

## What the model cannot show

The model is written from the datasheet, by the same reading as the driver. It cannot show:

- whether the GPIO levels latched on a CS falling edge already include outputs refreshed on that edge (the driver's
  `Verify::ReadBack` reads one frame later, so it does not depend on it);
- what the chip does with the word of the frame after an alarm exit bit (the model ignores it, the driver sends a
  "continue" word there);
- analog behaviour: acquisition and settling at your SCLK and source impedance, noise, the input range near +VA;
- electrical timing: the CS high time, SDO delays, a slow MISO edge at 4 MHz on a long link.

## Before you depend on it

Run these on your board, with the driver behind the code that owns it, and record the result below:

1. Board plugged: `EnsureInitialized()` returns `Ok` in 13 frames (logic analyser on CS: 13 CS pulses), 16 with
   a scale monitor declared (`Config::monitor`).
2. Board unplugged: `NoDevice` (last reply 0xFFFF) or `DeviceIdMismatch`, never `Ok`. SDI disconnected:
   `DeviceIdMismatch`.
3. `TraceFrames(ch, words)` with six words, on a channel `ch` with a known input: the address nibble of `words[2]`
   onwards is `ch` and the code matches the input; `ReadChannel(ch)` returns the same code.
4. `SetAuto1Channels(mask, Verify::ReadBack)` with your k-channel list, then 1,000 `ReadAuto1()` passes: each `Ok`,
   k frames each after the first (count CS pulses), the codes equal to a polled read of the same channels within
   1 to 2 LSB.
5. Power-cycle the analog board while `ReadAuto1()` runs, and again while `ReadChannel(3)` runs in a loop:
   `DeviceReset` (or `SequenceMismatch` / `ChannelMismatch`), `IsInitialized()` false and `NotInitialized` from every
   call, then `Ok` again after `EnsureInitialized()`, with the scale monitor's code back in its window (the range
   restored) and the GPIO0 alarm tripping again on an input above its threshold. Repeat with `ReadChannel(0)` and
   check that `ApplyConfig(GetConfig())` re-arms the alarm; repeat once more with `Config::park_channel` set: the
   loop of `ReadChannel(0)` returns `DeviceReset` within two calls of the power cycle.
6. Scope SCLK and CS at 4 MHz: 16 clocks per frame, CS high between frames.
7. With the monitor declared: lift REFP (or the monitor resistor) and start: `ScaleMismatch`. With GPIO2 as the range
   input, hold it at the other level: `RangeMismatch`.
8. Measure +VA at the chip under full load (every load on the supply running) and record the minimum: it is
   `va_min_mv`, and Range 2 needs it at 2 x VREF or more (5000 with a 2.5 V reference).

## Hardware verification record (this driver)

| Board | Driver commit | Procedure | Result | Evidence |
| --- | --- | --- | --- | --- |
| (none yet) | | | | |
