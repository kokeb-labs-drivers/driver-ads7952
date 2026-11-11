# Quickstart

Run the driver on your PC first, with no hardware; then read a channel from your MCU: wire the chip, write a small
port class, call two driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SLAS605 C:

    - Pins, packages and the GPIOs each package brings out: 6 Pin Configuration and Functions, [p. 5](https://www.ti.com/lit/ds/symlink/ads7952.pdf#page=5)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated ADS7952, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/ads7952_host_example
```

```text
ADS7952 driver 0.1.0 on the host
ads7952: initialised
CH3: 901 (549926 uV)
CH0=163 CH1=409 CH2=655 CH3=901 CH6=1638 CH7=1884 CH9=2375 CH11=2867
CH0=163 CH1=409 CH2=655 CH3=901 CH6=1638 CH7=1884 CH9=2375 CH11=2867
```

The calls in [`main.cpp`](../examples/host/main.cpp) are the ones below; only the port differs on an MCU. CI builds
and runs this example on every pull request.

## 1. Wire it

| MCU | ADS7952 (TSSOP-38) | Notes |
| --- | --- | --- |
| SPI SCLK | SCLK (pin 32) | Mode 0, up to 20 MHz; 1 MHz for bring-up on jumper wires |
| SPI MOSI | SDI (pin 33) | |
| SPI MISO | SDO (pin 34) | High impedance while CS is high, so the bus can be shared |
| GPIO or hardware CS | CS (pin 31) | Low for one 16-bit frame, high between frames |
| 1.7 V to +VA | +VBD (pin 36) | Digital I/O supply: the MCU's I/O voltage |
| 2.7 V to 5.25 V | +VA (pins 5, 29) | Analog supply, decoupled at the pins |
| 2.5 V reference | REFP, REFM (pins 4, 3) | 10 µF between them |
| | MXO to AINP (pins 7, 8), AINM (9) to ground | Directly, or through a buffer |

The GPIOs can stay unconnected for this example (they reset as inputs). The [Hardware](hardware.md) page has the full
pin list and the timing.

## 2. Add the driver

Add the repository as a submodule and link the `ads7952::ads7952` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-ads7952.git external/driver-ads7952
```

## 3. Write the port

The port is the only platform code: it brings the bus up, moves one 16-bit frame per call and waits. Replace the
`platform_*` calls with your SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hook and the transfer rules.

## 4. Read a channel

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. `EnsureInitialized()` calls your port's `EnsureInitialized()`, resets the chip through its GPIO program register,
   writes `Config{}` (the reset values: Range 1, every channel in Auto-1) and checks that the replies carry the
   channel addresses it selects: a wiring fault fails here, not later.
2. `ReadChannel(3)` selects channel 3 in Manual mode and returns its result two frames later, after checking that
   the reply carries address 3. Reading channel 3 again costs one frame.
3. `ToMicrovolts()` converts the 12-bit code with the configured range and reference.

This example is built by the host CI, so it compiles against the current headers.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Your port's `EnsureInitialized()`: the bus did not come up |
| `EnsureInitialized()` returns `NoDevice` | Nothing drives MISO: the supplies, CS and SDO ([Troubleshooting](troubleshooting.md)) |
| `EnsureInitialized()` returns `DeviceIdMismatch` | SDI, the SPI mode, CS toggling between frames ([Troubleshooting](troubleshooting.md#ensureinitialized-returns-deviceidmismatch)) |
| Codes that do not follow the input | MXO linked to AINP, the reference, the range ([Troubleshooting](troubleshooting.md)) |

## Next

- [API guide](api-guide.md): scans, the hot path, alarms, GPIOs, errors.
- [Configuration](configuration.md): every `Config` field, Kconfig.
- [Examples](examples.md): the host example and the MCU examples.
- [Platforms](platforms.md): which MCUs have a port and an example.
