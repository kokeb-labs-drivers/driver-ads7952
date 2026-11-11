# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Changed

- The repository moved to kokeb-labs-drivers; URLs follow.
- The Hardware page draws the chip from the new `docs/hardware.yml`: the TSSOP-38 pinout with each pin's port hook,
  the functional block diagram of section 8.2, the typical application of Figure 60 with its reference, supply and
  input capacitors, and the pin and parts tables, every fact with its datasheet section and page. The example
  supply voltages and the reference's own supply are marked as not confirmed. The hand-written pin table is gone.
- The `hardware-lessons-review` skill and `tools/preflight.sh` follow the template.

## [0.1.0] - 2026-11-09

### Breaking

- A reply the driver cannot account for now leaves a running driver uninitialised: after `ChannelMismatch`,
  `SequenceMismatch`, the new `Error::DeviceReset` (22), a `WriteMismatch` from a sequence read-back, or a failed
  `CheckScale()` (other than `Bus`), every call returns `NotInitialized` until `EnsureInitialized()` succeeds again.
  A mismatching reply from channel 0, the power-on channel (section 8.4.3), and a `CheckScale()` that finds Range 1
  where the driver sends Range 2 are `DeviceReset` instead of `ChannelMismatch`, `SequenceMismatch` or
  `RangeMismatch`. Handle `DeviceReset` and call `EnsureInitialized()` after any of these errors.
- `ApplyConfig()` on a running driver writes every register, changed or not (6 + 10 frames per alarm group, 36 on
  the ADS7952), instead of the differences from its own copy.

### Added

- `IsConfigUncertain()`: true after a setter or `ApplyConfig()` failed with `Bus`, or a `Verify::ReadBack` did not
  confirm the write (the frame may have reached the chip), until every register is written again:
  `ApplyConfig(GetConfig())`, or `Deinitialize()` then `EnsureInitialized()`. `GetConfig()` keeps the values last
  written successfully; alarm thresholds are never assumed set after a failed write.
- `Config::park_channel` (opt-in, `kNoPark` by default): after `ReadChannel(0)` the chip rests on this channel, so a
  reset while it would otherwise rest on channel 0, the power-on channel, shows as `DeviceReset` at the next read.
  `ReadChannel(0)` costs one frame more (4 when repeated).
- First version of the driver for the TI ADS7950, ADS7951, ADS7952 and ADS7953 12-bit SPI SAR ADCs, written on the
  driver template against datasheet SLAS605C.
- `ads7952::ADS7952<Port>` with the configuration in the constructor and one start-up call, `EnsureInitialized()`:
  two frames that end any open program sequence, a register reset through the GPIO program register, every program
  register written, and an identity check that selects two channels and demands their addresses back (a, a, b), which
  a stuck or patterned MISO, an echo and a deaf SDI cannot pass. `ApplyConfig()` writes only what changed;
  `Deinitialize()` powers the chip down.
- Conversions keyed on the channel address of every reply: `ReadChannel()` (1 to 3 frames), `ReadChannels()`
  (pipelined, list length + 2 frames), `ReadAuto1()` and `ReadAuto2()` (one frame per channel while the sequence
  runs), `TraceFrames()` for bring-up. `Error::ChannelMismatch` and `Error::SequenceMismatch`.
- Settings: `SetRange()` (the next read skips the old-range reply), `SetAuto1Channels()`, `SetAuto2LastChannel()`,
  `SetAlarmThresholds()`, `SetGpioOutputs()`, `ReadGpio()`; `Verify::ReadBack` per call reads back the GPIO levels or
  scans the new sequence (`Error::WriteMismatch`).
- `Config` with the reset value of every register, the variant, the package, the reference and the lowest
  guaranteed +VA (`va_min_mv`); a `constexpr` `Validate()` (Range 2 only while 2 x VREF <= `va_min_mv`),
  `ResetConfig()`, `CodeToMicrovolts()` (one LSB is full scale / 4096); Kconfig options for the part, package,
  range, voltages and sequences.
- A scale monitor: `Config::monitor` declares a channel with a voltage derived from REFP. `EnsureInitialized()`
  checks it once (3 frames), `CheckScale()` on demand, and `CheckScaleCode()` judges a code already read, such as the
  monitor's entry of each `Bank`, with no frame. `Error::RangeMismatch` (the chip converts in the other range: a
  GPIO2 strap or a reset) and `Error::ScaleMismatch` (the reference or the monitor input is wrong). `Validate()`
  refuses a monitor that cannot tell the two ranges apart.
- `Bank::range` and `ToMicrovolts(bank, channel)`: a scan always converts with the range it was made in.
- The "Reference, range and supplies" page: how a code becomes a voltage, choosing VREF and the range for a supply,
  the scale monitor, ratiometric sensors decoded against their measured supply, power and layout from the
  datasheet, a board checklist and a worked example of a 5 V sensor board.
- After a failed frame inside a program sequence the driver repeats the Auto-1 mask or closes the alarm sequence, so
  the chip never takes the next command as data (found by the bus-fault sweep and the fuzzer).
- Host tests over a protocol model of the four parts with the bus and board faults a real board can produce, every
  frame of every operation failed in turn; a libFuzzer target with its corpus replayed as a CTest; the host and
  ESP32 examples; the frame-pipeline figure. Not verified on hardware.
- `docs/limitations.md`: the design choices with the datasheet section each rests on, the limits of the design,
  what the model cannot show, and the bench steps to run before depending on the driver.

### Changed

- The `misra-review`, `cpp20-profile` and `new-driver-from-template` skills and `docs/repo-setup.md` follow the
  template: reading the MISRA report, the known tool misfires, enumeration ranges without a sentinel, and the
  settings tool.
- `tools/preflight.sh` and the `verify` and `hardware-lessons-review` skills follow the template: every check CI runs
  that a workstation can run, in one command, and the rules for device resets, faults the chip clears itself, dropped
  writes, switch-offs that no uncertainty refuses and a model that changes state on its own.
- The port concepts match each hook's address against exactly one `noexcept` signature with
  `detail::ExactHook<Port, Sig>::Take(&Port::Hook)` instead of probing it with a converting `detail::Exactly<T>`:
  no conversion operator is left. A misdeclared hook is still a compile error. `kAlwaysFalse` takes a class and
  the enumeration helpers an enumeration (`ClassType`, `Enumeration`), each with a compile-fail test. The
  `cpp20-profile` skill is the template's again.
- The repository documents only this driver against its datasheet: each design choice cites the datasheet
  section it rests on (or states its reasoning and a bench step), with no dated history. The copyright holder is
  `The driver-ads7952 authors` in `LICENSE`, every source header, `driver.yml` (`authors`) and the docs footer.
  `tools/check_standalone.py`, from the driver template, checks both in the repository checks.
- The driver follows the current handbook rules and the C++20 profile, from the driver template:
  - Every header has an `#ifndef` include guard named after its file (KB-CPP-019); no `#pragma` is left.
  - The bus requirements are `constexpr` in `inc/ads7952_spi_interface.hpp` (KB-CPP-123): `kSpiMode` 0,
    `kMsbFirst`, `kMaxClockHz` 20 MHz, `kMinCsHighNs` 20 (t_w1), `kCsSetupNs` 8 (t_su1), `kCsHoldNs` 0,
    `kQuietNs` 40 (t_q), `kSdoReleaseNs` 26 (t_d3), `kContinuousWord` and `kFrameBits` 16, from datasheet
    sections 7.3, 7.9 and 8.1. The host and ESP32 ports check their settings with `static_assert`, and the ESP32
    port refuses a clock above the limit and drives CS high before it enables the pin (KB-HW-018).
  - `SpiPort` checks each hook's exact parameter types, `noexcept` and `bool` result (KB-CPP20-006): a port whose
    `Transfer()` takes a narrower length or is not `noexcept` no longer compiles. `DelayUs` and `Log` are found by
    the named concepts `HasDelayUs` and `HasLog`, and a hook declared with another signature is a compile error
    (KB-CPP20-007). Compile-fail tests for each case.
  - `Error` is `[[nodiscard]]` (KB-CPP20-037), and so are the pure helpers. `GetConfig()` is `&`-qualified and
    deleted on a temporary driver (KB-CPP20-044).
  - Every target builds as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`, the C++20 deprecations as
    errors); `ads7952_header_checks` compiles each public header alone on GCC, Clang and the Cortex-M cross-build,
    and `examples/esp32/main/ads7952_cxx20_check.cpp` does the same inside the ESP-IDF build. The tests and fuzzers
    build with the standard library's assertions.
  - Bit work is in `uint32_t`, with every shift count masked or proved below the width (KB-CPP20-020, -021):
    no `unsigned long` or `UL` shifts (64 bits on the host, 32 on the targets), no promoted `uint8_t` or `uint16_t`
    operand of a shift, `~`, `|` or `^`, and no compound assignment that narrows (KB-CPP20-022). `<bit>` counts
    and rotates the channel masks (`std::popcount`, `std::rotr`, `std::countr_zero`).
  - `ToString(Error)` is a switch over every enumerator; `Validate()` checks enumerations with
    `detail::IsEnumerator()`; loops have no extra flag in their condition. The frame builders, the GPIO and mode
    encoders (now free `constexpr` functions), the reset configurations, the channel helpers, the identity channels
    and the code-to-voltage conversion are proved with `static_assert` against the datasheet tables.
  - The two frame buffers belong to the driver object instead of the call's stack.
  - The chip model (`tests/mock_ads7952.hpp`) includes no driver header: it has its own frame format, reset values,
    channel counts and power-up time with their datasheet sections, and `tests/test_model.cpp` checks them against
    the driver's at compile time (KB-TST-030).
  - `TraceFrames()` documents its side effect (the chip is left in Manual mode on the channel); the API guide lists
    the raw access, and the Status page lists the handbook rules the chip gives no handle for (no clear-on-read
    flag to latch, no ready bit, 0x0000 being a valid channel 0 reply).
  - The template's review skills (`cpp20-profile`, `misra-review`, `hardware-lessons-review`), the
    `safety-reviewer` agent, the updated `verify` skill and the `Handbook source checks` CI job, pinned at the
    handbook's C++20-profile commit; `_config/.clang-tidy` follows the handbook.
- The frame cost of every call is unchanged.

- CI runs fewer jobs, from the driver template: the host tests build GCC and Clang in one job; DCO, the org
  standard, Markdown and YAML lint, the handbook rules, template drift and cppcheck are steps of one `Repository
  checks` job (`ci-repo-checks.yml`, replacing `ci-handbook.yml`, `ci-cpp-analysis.yml`, `ci-markdown-lint.yml` and
  `ci-yaml-lint.yml`); cross-compile and host quality use ci-tools' one-job layouts. No check is dropped. The
  workflows have no manual (`workflow_dispatch`) trigger any more.
- MISRA C++:2023 conformance: the findings of the first scan are fixed, with no change in behaviour or frame
  cost. A failed read clears its codes with `std::fill`, whose result is `void` (Rule 0.1.2); a narrow operand
  meets a `U` literal, and a bitwise operator on two `uint16_t` constants works in `uint32_t` (Rule 7.0.5); an
  integer constant argument has the parameter's type through the `_u8`, `_u16` and `_u32` literals of
  `ads7952_detail.hpp`, which reject an out-of-range value at compile time (compile-fail test), and a narrow
  result that initialises a `uint32_t` is cast (Rule 7.0.6); comparisons and arithmetic inside `&&`, `||`, `?:`
  and comparisons are parenthesised (Rule 8.0.1); loop bounds are `const` locals (Rule 9.5.1); every switch over
  an enumeration ends with `default: break;` and keeps its fallback, and the host tests build with
  `-Wswitch-enum` so a new enumerator still warns (Rule 9.4.2); `modeFrame()` has the same parameter name in its
  declaration and definition (Rule 13.3.3); the `.ipp` include of `ads7952.hpp` carries the deviation marker
  `MD-CPP-009` (Rule 19.0.3). clang-tidy's `modernize-use-ranges` is off: it would bring the discarded
  `std::ranges` results back.

### Fixed

- A reset behind the driver's back (a brown-out: every register back to its reset value, section 8.4.3, Table 11,
  section 8.5.3, and an invalid first conversion, section 7.6) is detected wherever a reply can show it, and the
  driver no longer goes on as if nothing happened. The frame that returns a `ReadChannel()` result is the Manual
  word for the channel with the range and GPIO levels, not a "continue" word (same frame count), so the range comes
  back at the next read. The replies a new select, a `ReadChannels()` list or an Auto entry discards must carry the
  Manual channel the chip had settled on. Each frame of an Auto-1 or Auto-2 pass is the Auto word with DI11 = 1 and
  no restart instead of a "continue" word (same frame count): it re-sends the range, and after a reset it makes the
  chip enter the mode with the reset sequence, so a sequence of channel 0 alone shows the reset too.
  `ApplyConfig(GetConfig())` restores the whole configuration, the alarm function and thresholds included. A reset
  at any moment the chip rests on channel 0 in Manual mode (in any mix of reads, without `Config::park_channel`)
  shows in no reply, and up to two wrong results can be returned after a reset before it is seen; the limits and the
  interlock guidance are in `docs/limitations.md`.
- The chip model can reset on its own before a chosen frame (`brownout_before_frame`), with the conversion in flight
  kept or lost; `tests/test_reset.cpp` covers the reset on every read path and the alarm interlock, and the fuzzer
  checks that every mismatch leaves the driver uninitialised.
- `readPipelined()`, which indexes the caller's spans, checks their sizes itself (KB-CPP20-043); before, only its
  caller did.
- `AllChannels()` and `detail::Auto2Mask()` shifted `unsigned long`, which is 64 bits on the host and 32 on the
  targets, so a width bug could not show in the host tests; they shift `uint32_t` now.
- `ValidateGpio()` and the alarm writer narrowed in `|=` (`uint8_t |= unsigned`); they collect in `uint32_t`.

