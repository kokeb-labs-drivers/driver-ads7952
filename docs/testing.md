# Testing and CI

How the driver is checked: host tests against a model of the chip, a fuzzer that corrupts what the chip sends
back, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality
gates, on every pull request.

## Host tests

The tests build the real driver against a protocol model of the chip (`tests/mock_ads7952.hpp`) that runs it frame by
frame: the sample taken at each CS falling edge of the channel acquired in the frame before, the multiplexer switching
as Manual, Auto-1 and Auto-2 say, the range that applies to each acquisition, DI11 gating the mode word, "continue"
words, the GPIO register with its reset in the next frame, the two-frame Auto-1 program, the Auto-2 program that
restarts the sequence, the alarm sequence with its exit frame, the alarm and GPIO pins, power-down and wake-up, the
invalid first conversion and the power-up time. The faults are the bus and board conditions a broken or floating line
produces, and the ones the datasheet names: MISO floating high, stuck low, a fixed word (0x0E83), MOSI echoed or
noise; SDI not reaching the chip; a brown-out that resets every register, poked by a test or on its own before a
chosen frame, with the conversion in flight kept or lost; a GPIO pin held by the board; failed
transfers (before and after the chip took the frame), lost and replaced transfers. They build with the firmware's
flags, `-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang. `ads7952_host_tests` runs 81 test cases;
with the Kconfig, version, install, Doxygen, host-example, compile-fail and fuzz-corpus tests CTest runs 21 tests.

```bash
cmake -S . -B build -DADS7952_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_ads7952.cpp`](../tests/test_ads7952.cpp) | The lifecycle (no I/O before the start, the reset and the frames of the start, a retried start, a chip left inside a program sequence, restart from power-down), `Validate()`, the identity check against every bus fault and on every variant, Manual reads and their frame counts, pipelined lists, Auto-1 and Auto-2 scans (entry, the running sequence, a pass starting anywhere, a lost mask, a brown-out), `SetRange()` and the skipped reply, program registers with and without `Verify::ReadBack`, alarms and alarm pins, GPIOs and their read-back, `ApplyConfig()` before and after the start, logging |
| [`tests/test_bus_faults.cpp`](../tests/test_bus_faults.cpp) | Every frame of every operation, `EnsureInitialized()` included, fails in turn, before and after the chip took it; each returns `Bus` and the next read works; every frame of a read is lost in turn and none yields another channel's result; the repairs after a failed mask or alarm frame |
| [`tests/fuzz/`](../tests/fuzz/fuzz_ads7952.cpp) | The fuzz target; CTest replays its corpus (`ads7952_fuzz_ads7952_corpus`), see below |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | Every chip-specific value (16 to 22) and `Unsupported` are returned by a real call |
| [`tests/test_reset.cpp`](../tests/test_reset.cpp) | A reset the chip does on its own mid-session (`brownout_before_frame`, with the conversion in flight kept or lost): the model reloads the reset values and gives one invalid result; `DeviceReset` from a read, a select, a list, an Auto-1 scan and `CheckScale()`; the driver uninitialised after every mismatch; `ApplyConfig()` writing every register on every part; the GPIO0 alarm re-armed |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/port_without_delay.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a port without `DelayUs()`, a `void Transfer()`, a `Transfer()` with a narrowed length or without `noexcept`, a `DelayUs(uint16_t)`, a `Log()` without `noexcept`, a class that is not a port |
| [`tests/mock_ads7952.hpp`](../tests/mock_ads7952.hpp) | The chip model, with its own frame format; the mock ports over it are in `mock_ads7952_bus.hpp` |
| [`tests/test_model.cpp`](../tests/test_model.cpp) | The model's frame format, reset values, channel counts and timing against the driver's, with `static_assert` |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`ads7952_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(ads7952)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

The model does not reproduce the chip's analog or electrical behaviour, and two points the datasheet leaves open
are modelled the conservative way: see [Status and limitations](limitations.md).

### The model stands alone

[`tests/mock_ads7952.hpp`](../tests/mock_ads7952.hpp) is written from the datasheet, not from the driver: it does
not include `ads7952_registers.hpp` or `ads7952_types.hpp`. Its command codes, bit positions, reset values, channel
counts and power-up time are its own (namespace `ads7952_test::chip`, each with the datasheet section), so a wrong
constant in the driver cannot hide behind the same wrong constant in the model (handbook KB-TST-030).
[`tests/test_model.cpp`](../tests/test_model.cpp) compares the two at compile time:

```cpp
--8<-- "tests/test_model.cpp:cross-check"
```

When the build stops on one of these, one of the two is wrong: check it against the section the model names, and
fix that one. Every behaviour found on hardware becomes a fault mode of the model and a test that fails on the code
before the fix; the model's virtual clock is `Elapse()`, driven by the mock port's `DelayUs()`.

### Strict dialect

Every target builds as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`, the C++20 deprecations as
errors), and the target `ads7952_header_checks` compiles each public header alone and asserts
`__cplusplus == 202002L` (handbook KB-CPP20-001). It builds with the host tests on GCC and Clang and with the
Cortex-M cross-build; `examples/esp32/main/ads7952_cxx20_check.cpp` does the same inside the ESP-IDF build, which
otherwise compiles C++ in a newer GNU dialect. The tests and fuzzers build with the standard library's assertions
(`_GLIBCXX_ASSERTIONS`, libc++ hardening), so an out-of-range span access traps.

## Fuzz tests

Everything the driver receives comes from MISO, so MISO is what a noisy cable, an unplugged board or a broken chip
controls. [`tests/fuzz/fuzz_ads7952.cpp`](../tests/fuzz/fuzz_ads7952.cpp) lets libFuzzer choose the part, the
package and a configuration (sometimes for another part, sometimes invalid), then up to 64 driver calls, and on every
transfer what the bus does: pass the model's reply, fail the transfer, lose it (the chip never sees it, MISO floats),
flip bits of the reply or replace it. Between calls it changes the board: MISO floating, stuck low, a fixed word,
echoing or noisy, SDI not reaching the chip, a brown-out, the analog inputs, the GPIO inputs, a pin held low. After
every call the target checks the driver's promises and aborts, naming the line, when one breaks:

- a failed call returns an `Error` the enum defines (never one documented as not returned) and `T{}`; every
  transfer is one 2-byte frame with both buffers;
- before `EnsureInitialized()` no call makes a transfer, and `InvalidArgument` means nothing was sent;
- **`EnsureInitialized()` never returns `Ok` on a dead bus** (MISO floating, stuck, a fixed word, an echo, or a deaf
  chip) unless a transfer of the call was tampered;
- **a conversion never returns another channel's result**: without a tampered or untrusted transfer in the call,
  every code it returns is one the chip sent for that channel in that call;
- while the bus has been clean since the start, every code is the chip's conversion of that channel in the
  configured range, and the chip's program registers and range are what the driver was told.
- after `DeviceReset`, `ChannelMismatch` or `SequenceMismatch` the driver is uninitialised.

The fuzzer found two bugs, both fixed with a unit test and a `regress-*` input each:

- A failed alarm frame, then a failed repair frame, left the chip inside the alarm sequence; the next
  `EnsureInitialized()`'s reset word was taken as a threshold, so the start went ahead without a reset. Every start
  now opens with two frames that end any program sequence (`regress-reset-eaten-by-alarm-sequence`).
- A chip whose SDI broke while it ran an Auto-1 sequence of exactly the two identity channels answered the identity
  check's two selects in turn and passed it. The check now asks for the first address twice, which no deaf chip can
  answer (`regress-deaf-chip-scanning-identity-pair`).

The bus-fault sweep found a third before the fuzzer ran: a chip that saw the Auto-1 request but not the mask took the
next Manual word as its mask. The driver now repeats the mask frame after a failure, and closes an alarm sequence.

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM and its libc++)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DADS7952_BUILD_TESTS=ON -DADS7952_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_ads7952
./build-fuzz/tests/fuzz/fuzz_ads7952 -max_total_time=300 tests/fuzz/corpus/fuzz_ads7952
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_ads7952`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_ads7952/`](../tests/fuzz/corpus/fuzz_ads7952) on GCC and Clang: the seeds, and every input
that ever broke a promise (`regress-*`), so a fixed bug stays fixed. CI fuzzes for 120 s on every pull request
(job `Host quality / fuzz`). After the fixes, local runs of 5 and 25 minutes broke no promise.

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` | This driver (local run) |
| --- | --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 | highest 14 (`Validate()`); 11 in the driver class (`writeAlarmGroup()`, `initialize()`) |
| Function length | lizard, lines of code per function | at most 60 | within the limit |
| Parameters | lizard, per function | at most 6 | within the limit |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % | 100 % |
| Branch coverage | same run | at least 75 % | 98.2 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) | 98.6 % |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report | clean in CI |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise | clean locally after the fixes (5 and 25 minutes) |

The local figures are GCC 15 and gcovr 8.7 with the CI's options; CI uses GCC 14. The reports are in the run's
`host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DADS7952_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc/*.hpp src/*.ipp   # list the files: lizard skips .ipp in a directory

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DADS7952_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `ads7952_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access, the C++20 profile) |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

The driver is **not verified on hardware** yet. The ESP32 example ([Examples](examples.md)) is a first bench test:
it starts the chip and logs one Auto-1 scan twice a second; unplug the board and plug it back while it runs. The
checks that count, behind your handler on your board, are listed in
[Status and limitations](limitations.md#before-you-depend-on-it). Record the board, the driver commit and the result
there.
