# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - Supplies, reference and SCLK limits: 7.3 Recommended Operating Conditions, [p. 10](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=10)
    - Serial timing, CS high time and the 16-clock frame: 7.9 Timing Requirements; Figures 1 and 2, [p. 17](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=17)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `ads7952::SpiInterface<Self>` (in
[`inc/ads7952_spi_interface.hpp`](../inc/ads7952_spi_interface.hpp)). The base uses CRTP and declares no hook
itself; the driver template is constrained by the `ads7952::SpiPort` concept, so calls are resolved at compile
time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the point
of use.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the bus up on the first call; return `true` at once afterwards. `false` makes the driver's `EnsureInitialized()` return `Bus` |
| `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | yes | One 16-bit frame: `len` is always 2 and both buffers are set; CS low for both bytes and high after. `false` on any bus error |
| `void DelayUs(uint32_t us) noexcept` | yes | Wait at least `us` microseconds; used once, for the power-up time after the start-up reset |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the transfer must follow:

- **One call, one frame.** Keep CS low for exactly the 16 clocks of the 2 bytes and raise it after: the chip samples
  its input and starts a conversion on every CS falling edge, so CS must toggle between frames (at least 20 ns high,
  section 7.9). A port that bursts several frames under one CS breaks the chip's frame count.
- **The bus requirements below**: mode 0, MSB first, up to `kMaxClockHz` (20 MHz), the 16 clocks of a frame back
  to back (8-bit words with no idle clock between the two bytes, or one 16-bit word). Start at 1 MHz on jumper
  wires.
- **Mind the acquisition time.** The next channel acquires from the 14th clock of a frame to the next CS falling
  edge (section 8.1). A faster clock and back-to-back frames shorten it; pick the rate for your input network (a
  board running SPI at 4 MHz with software-timed frames sees gaps of tens of microseconds).
- **Raw bytes only.** Do not interpret, retry or reorder bytes; the driver owns the framing and the retries.
- **Return `false` for a bus error.** The driver turns it into `Error::Bus` and never ignores it. A port that cannot
  report a failure would turn every failed transfer into a frame of data; a `void` `Transfer()` is a compile error.
- **Bounded time-outs.** A stuck bus fails the call; it does not block forever.

## Bus requirements

The driver states what the ADS795x needs from the bus as `constexpr` values in
[`inc/ads7952_spi_interface.hpp`](../inc/ads7952_spi_interface.hpp) (handbook KB-CPP-123). Check your port's
settings against them with `static_assert`, as both example ports do, so a port that clocks too fast or in the
wrong mode does not compile. The values hold for every +VBD from 1.7 V to 5.25 V (the largest minimum of section
7.9 is taken):

| Constant | Value | Meaning (datasheet) |
| --- | --- | --- |
| `kSpiMode` | `0` | CPOL 0, CPHA 0: SDI latched on the rising edge, SDO changes on the falling edge (8.1, Figure 2) |
| `kMsbFirst` | `true` | DI15 and DO15 first (Figure 2) |
| `kMaxClockHz` | `20'000'000` | f(SCLK) max (7.3, 7.9) |
| `kMinCsHighNs` | `20` | CS high between two frames, t_w1 (7.9) |
| `kCsSetupNs` | `8` | CS low to the first rising SCLK edge, t_su1 at +VBD = 1.8 V (7.9) |
| `kCsHoldNs` | `0` | 7.9 gives no minimum: CS may rise once the 16th SCLK falling edge has passed (8.1) |
| `kQuietNs`, `kSdoReleaseNs` | `40`, `26` | SDO 3-state to the next CS falling edge, t_q; the 16th SCLK falling edge to SDO 3-state, t_d3 (7.9). The 16th falling edge comes at least 66 ns before the next CS falling edge |
| `kContinuousWord` | `true` | No idle SCLK between the two bytes: the chip converts with SCLK (t_conv = 16 SCLK) and the datasheet does not specify a conversion with the clock stopped |
| `kFrameBits` | `16` | One frame is one 16-bit word with CS low throughout (8.1) |

## Control pins

The ADS795x has no reset, enable or other control input: its only control line is CS, and the port (or the SPI
peripheral) owns it. CS idles high. Drive it high before its output is enabled and before the first frame, so the
chip sees no CS falling edge it was not sent (handbook KB-HW-018); the ESP32 port does this before
`spi_bus_add_device()`, and a pull-up on CS keeps it high while the MCU resets. GPIO0 to GPIO3 are inputs after a
reset (Table 11) and the driver sets them from `Config::gpio`; the driver has no `GpioSet()` hook.

## Optional hooks

The driver finds the optional hooks at compile time with named concepts in one place (`ads7952::detail`:
`HasDelayUs`, `HasLog`), which check the exact signature, `noexcept` and the result type. A missing hook falls back
in a documented, safe way or fails to compile; a hook that is declared with another signature (`DelayUs(uint16_t)`,
a `Log` without `noexcept`) is a compile error, never skipped silently.

| Hook | Used for | Without it |
| --- | --- | --- |
| `DelayUs()` | The power-up time (`kPowerUpUs`, 1 µs) after the start-up reset | Compile error at `EnsureInitialized()` |
| `Log()` | One line per lifecycle event or failure | Nothing is logged |

A port with every hook:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

The required hooks are checked as exactly: a `Transfer()` that returns `void`, takes a narrower length
(`uint16_t len`) or is not `noexcept` does not satisfy `SpiPort` (handbook KB-CPP20-006), so it is a compile
error instead of a silent truncation. The [compile-fail tests](testing.md) check each case, and the missing
`DelayUs()`.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_ads7952_port.hpp`](../examples/host/host_ads7952_port.hpp): any PC, against a simulated
  chip. Start from it for a host build or a test bench; it is also the smallest complete port.
- [`examples/esp32/main/esp32_ads7952_port.hpp`](../examples/esp32/main/esp32_ads7952_port.hpp): ESP-IDF, with
  every hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that SDI reaches the chip and SDO comes back, frame by frame (the
   identity check reads back the channel addresses it selects).
3. Call `TraceFrames(ch, words)` with a channel wired to a known voltage: from `words[2]` on the address is `ch` and
   the code matches the voltage (`ToMicrovolts()`).

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
