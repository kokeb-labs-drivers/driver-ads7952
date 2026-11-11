# ADS7952 host example

The driver on your PC, with no hardware: the host port in
[`host_ads7952_port.hpp`](host_ads7952_port.hpp) connects the real driver to a simulated ADS7952 (the register model
the host tests use), and [`main.cpp`](main.cpp) initialises it and reads the temperature three times.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/ads7952_host_example
```

## Expected output

```text
ADS7952 driver 0.1.0 on the host
ads7952: initialised
25.00 C
25.00 C
25.00 C
```

The host tests build this folder too and run the example (CTest `ads7952_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: initialise, read, print |
| [`host_ads7952_port.hpp`](host_ads7952_port.hpp) | The host port: bus hooks over the simulated chip, `DelayUs()` and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
