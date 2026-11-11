/**
 * @file fuzz_ads7952.cpp
 * @brief Fuzz target: the driver over a chip model and a bus that the fuzzer controls.
 *
 * Everything the driver receives comes from MISO, so MISO is the untrusted input. The fuzzer's
 * bytes choose a configuration and the `Verify` value of the setters, then a sequence of driver
 * calls; on every transfer they also choose what the bus does: pass the chip model's reply, fail
 * the transfer, lose it (the chip never sees it and MISO floats), flip bits of the reply or
 * replace it. Between calls they change the board: MISO floating high, stuck low, a fixed pattern,
 * MOSI echoed or noise, SDI not reaching the chip, a brown-out that resets the chip, the analog
 * inputs, the GPIO inputs and a pin held by the board.
 *
 * A transfer is "tampered" when the fuzzer changed it (failed, lost, flipped or replaced) or MISO
 * carried noise, and "untrusted" when its reply did not come from the chip (tampered, or MISO
 * floating, stuck, a pattern or an echo). After every call the target checks the driver's promises
 * (docs/contract.md), and aborts, naming the line, when one breaks:
 *
 * - a failed call carries an `Error` the enum defines and the driver documents, and a value of
 *   `T{}`; every transfer is one 2-byte frame;
 * - before `EnsureInitialized()` succeeds, no call makes a transfer; `InvalidArgument` means
 *   nothing was sent;
 * - `EnsureInitialized()` never returns `Ok` on a dead bus (MISO floating, stuck, a pattern, an
 *   echo, or SDI not reaching the chip) unless a transfer of the call was tampered;
 * - a conversion that returns `Ok` without an untrusted transfer returns, for each channel, a code
 *   the chip really sent for that channel in this call: never another channel's result;
 * - while the bus has been clean since the start (no tampered or untrusted transfer, no brown-out),
 *   every code returned is the chip's conversion of that channel in the configured range, and a
 *   setting that returned `Ok` is what the chip holds;
 * - a setter or `ApplyConfig()` that fails with `Bus` leaves `IsConfigUncertain()` true (the write
 * may have taken effect), and a successful `ApplyConfig()` clears it;
 * - a reply the driver could not account for (`ChannelMismatch`, `SequenceMismatch`,
 *   `DeviceReset`) leaves the driver uninitialised: it never goes on from a configuration the chip
 *   may have lost.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang, `-DADS7952_BUILD_FUZZERS=ON`),
 * and as a plain executable that replays the corpus in tests/fuzz/corpus/ as a CTest on any
 * compiler.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"

namespace {

using ads7952::Error;
using ads7952::Verify;
using ads7952_test::Ads7952Model;
using ads7952_test::Miso;
namespace reg = ads7952::reg;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const uint32_t high = Byte();
    return static_cast<uint16_t>((high << 8U) | static_cast<uint32_t>(Byte()));
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_ads7952: promise broken at line %d\n", line);
    std::abort();
  }
}

/// The SPI port: the chip model behind a bus the fuzzer controls.
class FuzzPort : public ads7952::SpiInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return bus_up_ok_;
  }

  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    Check(tx != nullptr && rx != nullptr && len == ads7952::kFrameBytes);
    transfers_++;
    const uint8_t action = in_.Byte();
    if (action == 0xFFU) {
      tampered_++;
      return false; // the bus failed; the frame never reached the chip
    }
    const auto word = static_cast<uint16_t>((static_cast<unsigned>(tx[0]) << 8U) | tx[1]);
    const bool chip_drives = chip_.miso == Miso::Chip;
    uint16_t reply = 0;
    bool genuine = chip_drives;
    if (action == 0xFEU) {
      tampered_++; // lost: the chip never sees it, MISO floats
      genuine = false;
      reply = (in_.Byte() & 1U) != 0U ? 0xFFFFU : 0x0000U;
    } else {
      reply = chip_.Frame(word);
      if (action == 0xFDU || action == 0xFCU) {
        tampered_++;
        genuine = false;
        reply = action == 0xFDU ? static_cast<uint16_t>(reply ^ in_.Word()) : in_.Word();
      }
    }
    if (chip_.miso == Miso::Noise) {
      tampered_++;
    }
    untrusted_ += genuine ? 0 : 1;
    if (genuine && genuine_count_ < genuine_.size()) {
      genuine_[genuine_count_++] = reply;
    }
    rx[0] = static_cast<uint8_t>(reply >> 8U);
    rx[1] = static_cast<uint8_t>(reply & 0xFFU);
    return true;
  }

  void DelayUs(uint32_t us) noexcept {
    chip_.Elapse(us);
  }

  Ads7952Model& Chip() noexcept {
    return chip_;
  }
  [[nodiscard]] int Transfers() const noexcept {
    return transfers_;
  }
  [[nodiscard]] int Tampered() const noexcept {
    return tampered_;
  }
  [[nodiscard]] int Untrusted() const noexcept {
    return untrusted_;
  }
  void SetBusUp(bool ok) noexcept {
    bus_up_ok_ = ok;
  }
  /// Forget the replies of earlier calls.
  void NewCall() noexcept {
    genuine_count_ = 0;
  }
  /// The chip sent `code` for `channel` during this call.
  [[nodiscard]] bool Sent(uint8_t channel, uint16_t code) const noexcept {
    for (std::size_t i = 0; i < genuine_count_; ++i) {
      if (reg::ReplyAddress(genuine_[i]) == channel && reg::ReplyCode(genuine_[i]) == code) {
        return true;
      }
    }
    return false;
  }

private:
  Input& in_;
  Ads7952Model chip_;
  std::array<uint16_t, 256> genuine_{};
  std::size_t genuine_count_ = 0;
  int transfers_ = 0;
  int tampered_ = 0;
  int untrusted_ = 0;
  bool bus_up_ok_ = true;
};

using Driver = ads7952::ADS7952<FuzzPort>;

bool KnownError(Error e) noexcept {
  switch (e) {
  case Error::Ok:
  case Error::NotInitialized:
  case Error::InvalidArgument:
  case Error::Bus:
  case Error::DeviceIdMismatch:
  case Error::Unsupported:
  case Error::NoDevice:
  case Error::ChannelMismatch:
  case Error::SequenceMismatch:
  case Error::WriteMismatch:
  case Error::RangeMismatch:
  case Error::ScaleMismatch:
  case Error::DeviceReset:
    return true;
  case Error::Timeout: // documented as never returned
  case Error::Crc:
  case Error::Busy:
  case Error::DeviceFault:
    return false;
  }
  return false;
}

/// The fuzzer's state between calls.
struct World {
  Verify verify = Verify::None;
  bool clean = false; ///< No tampered or untrusted transfer, no board fault, since the start.
};

/// A configuration from the fuzzer's bytes, often invalid on purpose. The variant and package
/// match the chip unless the fuzzer says otherwise.
ads7952::Config FuzzConfig(Input& in, const Ads7952Model& chip, World& w) noexcept {
  const uint8_t a = in.Byte();
  const uint8_t b = in.Byte();
  ads7952::Config c = ads7952::ResetConfig(ads7952_test::DriverVariant(chip.variant));
  c.package = ads7952_test::DriverPackage(chip.package);
  if ((a & 0x07U) == 0x07U) {
    c.variant = static_cast<ads7952::Variant>(in.Byte() % 5U); // 4 is not an enumerator
  }
  c.range = (a & 0x08U) != 0U ? ads7952::Range::TwoVref : ads7952::Range::Vref;
  c.va_min_mv = (a & 0x10U) != 0U ? uint16_t{4900} : uint16_t{5000};
  w.verify = (a & 0x20U) != 0U ? Verify::ReadBack : Verify::None;
  if ((a & 0x40U) != 0U) {
    c.auto1_channels = in.Word();
    c.auto2_last_channel = static_cast<uint8_t>(in.Byte() % 17U);
  }
  c.gpio.output_mask = static_cast<uint8_t>(b & 0x0FU);
  c.gpio.output_levels =
      static_cast<uint8_t>((static_cast<uint32_t>(b) >> 4U) & c.gpio.output_mask);
  if (c.gpio.output_mask == 0U) {
    // A scale monitor on the last channel, at the model's input there: no extra input byte, so
    // the corpus keeps its meaning.
    const auto last =
        static_cast<uint8_t>(ads7952::ChannelCount(ads7952_test::DriverVariant(chip.variant)) - 1U);
    c.monitor = {.channel = last,
                 .expected_uv = static_cast<uint32_t>(chip.input_mv[last]) * 1000U,
                 .tolerance_uv = 100000U};
  }
  if ((a & 0x80U) != 0U) {
    c.gpio.alarm_output = static_cast<ads7952::AlarmOutput>(in.Byte() % 8U);
    const uint8_t ch = in.Byte() % 16U;
    c.alarms[ch] = {static_cast<uint16_t>(in.Word() % 4200U),
                    static_cast<uint16_t>(in.Word() % 4200U)};
    const uint8_t park = in.Byte() % 17U; // 0 and channels the part lacks are invalid on purpose
    c.park_channel = (park == 16U) ? ads7952::kNoPark : park;
  }
  return c;
}

/// The board between calls: what a cable, a bench or a worn part does to the chip.
void FuzzBoard(FuzzPort& port, Input& in, World& w) noexcept {
  Ads7952Model& chip = port.Chip();
  const uint8_t a = in.Byte();
  const uint8_t b = in.Byte();
  chip.miso = (a & 0x07U) < 4U ? Miso::Chip : static_cast<Miso>(1U + (a % 5U));
  chip.pattern = (a & 0x08U) != 0U ? in.Word() : uint16_t{0x0E83};
  chip.deaf_sdi = (a & 0x30U) == 0x30U;
  if ((a & 0xC0U) == 0xC0U) {
    chip.Brownout((b & 0x40U) != 0U ? Ads7952Model::Pipeline::Lost : Ads7952Model::Pipeline::Kept);
    w.clean = false;
  }
  chip.input_mv[b % 16U] = static_cast<uint16_t>(in.Word() % 5600U);
  chip.gpio_inputs = static_cast<uint8_t>(in.Byte() & 0x07U); // GPIO3 high: never powered down
  chip.gpio_held = static_cast<uint8_t>((static_cast<uint32_t>(b) >> 4U) & 0x03U);
  chip.noise_state = in.Word();
  port.SetBusUp((in.Byte() & 0x0FU) != 0U);
  if (chip.miso != Miso::Chip || chip.deaf_sdi) {
    w.clean = false; // stays false until the next start on a working bus
  }
}

/// What a call may change, taken before it.
struct Before {
  bool up = false;
  int transfers = 0;
  int tampered = 0;
  int untrusted = 0;
};

Before Snapshot(const Driver& d, FuzzPort& port) noexcept {
  port.NewCall();
  Before b{};
  b.up = d.IsInitialized();
  b.transfers = port.Transfers();
  b.tampered = port.Tampered();
  b.untrusted = port.Untrusted();
  return b;
}

/// Promises every call keeps; returns true when the call's transfers were all trusted.
bool CheckCommon(const Driver& d, FuzzPort& port, const Before& b, Error e, bool lifecycle,
                 World& w) noexcept {
  Check(KnownError(e));
  if ((e == Error::ChannelMismatch) || (e == Error::SequenceMismatch) ||
      (e == Error::DeviceReset)) {
    Check(!d.IsInitialized()); // fail-safe after a reply the driver cannot account for
  }
  const int sent = port.Transfers() - b.transfers;
  if (!b.up && !lifecycle) {
    Check(e == Error::NotInitialized && sent == 0);
  }
  if (e == Error::InvalidArgument) {
    Check(sent == 0);
  }
  const bool trusted = port.Untrusted() == b.untrusted && port.Tampered() == b.tampered;
  w.clean = w.clean && trusted;
  return trusted;
}

/// A code the driver returned for `channel`.
void CheckCode(const Driver& d, FuzzPort& port, bool trusted, const World& w, uint8_t channel,
               uint16_t code) noexcept {
  Check(code <= ads7952::kMaxCode);
  if (trusted) {
    Check(port.Sent(channel, code)); // never another channel's result, never made up
  }
  if (w.clean) {
    const bool two = d.GetConfig().range == ads7952::Range::TwoVref;
    Check(code == port.Chip().CodeOf(channel, two));
  }
}

/// While the bus is clean, the program registers are what the driver was told.
void CheckRegisters(const Driver& d, FuzzPort& port, const World& w) noexcept {
  if (!w.clean || !d.IsInitialized()) {
    return;
  }
  const ads7952::Config& c = d.GetConfig();
  Check(port.Chip().auto1_mask == c.auto1_channels);
  Check(port.Chip().auto2_last == c.auto2_last_channel);
  Check((port.Chip().gpio_register & 0x0FU) ==
        (c.gpio.output_mask | (c.package == ads7952::Package::Vqfn ? 0x0EU : 0x00U)));
  Check(port.Chip().two_vref == (c.range == ads7952::Range::TwoVref));
}

void FuzzLifecycle(Driver& d, FuzzPort& port, Input& in, World& w, uint8_t op) {
  const Before b = Snapshot(d, port);
  if (op == 0) {
    const bool dead = port.Chip().miso != Miso::Chip && port.Chip().miso != Miso::Noise;
    const bool deaf = port.Chip().deaf_sdi;
    const auto r = d.EnsureInitialized();
    const bool trusted = CheckCommon(d, port, b, r.error, true, w);
    Check(r.ok() == d.IsInitialized());
    if (b.up) {
      Check(r.ok() && port.Transfers() == b.transfers); // idempotent, no I/O once started
    } else if (r.ok()) {
      Check((!dead && !deaf) || port.Tampered() > b.tampered); // a dead bus never passes
      const bool same_part =
          d.GetConfig().variant == ads7952_test::DriverVariant(port.Chip().variant) &&
          d.GetConfig().package == ads7952_test::DriverPackage(port.Chip().package);
      w.clean = trusted && !dead && !deaf && same_part;
    }
  } else if (op == 1) {
    const auto r = d.Deinitialize();
    CheckCommon(d, port, b, r.error, true, w);
    Check(!d.IsInitialized());
  } else {
    const auto r = d.ApplyConfig(FuzzConfig(in, port.Chip(), w));
    CheckCommon(d, port, b, r.error, true, w);
    if (b.up) {
      Check(d.IsConfigUncertain() == (r.error == Error::Bus) || r.error == Error::InvalidArgument ||
            r.error == Error::Unsupported);
    }
    if (!b.up) {
      Check(port.Transfers() == b.transfers); // stored only
    }
  }
  CheckRegisters(d, port, w);
}

void FuzzConversion(Driver& d, FuzzPort& port, Input& in, World& w, uint8_t op) {
  const Before b = Snapshot(d, port);
  if (op == 3) {
    const uint8_t ch = in.Byte() % 17U;
    const auto r = d.ReadChannel(ch);
    const bool trusted = CheckCommon(d, port, b, r.error, false, w);
    Check(r.ok() || r.value == 0);
    if (r.ok()) {
      CheckCode(d, port, trusted, w, ch, r.value);
    }
  } else if (op == 4) {
    std::array<uint8_t, 18> chans{};
    std::array<uint16_t, 18> codes{};
    const std::size_t n = in.Byte() % 19U;
    for (std::size_t i = 0; i < n; ++i) {
      chans[i] = in.Byte() % 17U;
    }
    codes.fill(0xBEEF);
    const auto r = d.ReadChannels(std::span<const uint8_t>(chans.data(), n),
                                  std::span<uint16_t>(codes.data(), n));
    const bool trusted = CheckCommon(d, port, b, r.error, false, w);
    for (std::size_t i = 0; i < n; ++i) {
      if (r.ok()) {
        CheckCode(d, port, trusted, w, chans[i], codes[i]);
      } else if (r.error != Error::InvalidArgument && r.error != Error::NotInitialized) {
        Check(codes[i] == 0); // no partial result
      }
    }
  } else if (op == 5 || op == 6) {
    const auto r = op == 5 ? d.ReadAuto1() : d.ReadAuto2();
    const bool trusted = CheckCommon(d, port, b, r.error, false, w);
    Check(r.ok() || r.value.mask == 0);
    if (r.ok()) {
      const ads7952::Config& c = d.GetConfig();
      Check(r.value.mask ==
            (op == 5 ? c.auto1_channels : ads7952::detail::Auto2Mask(c.auto2_last_channel)));
      for (uint8_t ch = 0; ch < ads7952::kMaxChannels; ++ch) {
        if (r.value.Has(ch)) {
          CheckCode(d, port, trusted, w, ch, r.value.code[ch]);
        } else {
          Check(r.value.code[ch] == 0);
        }
      }
    }
  } else {
    std::array<uint16_t, 40> words{};
    const std::size_t n = in.Byte() % 40U;
    const auto r = d.TraceFrames(in.Byte() % 17U, std::span<uint16_t>(words.data(), n));
    CheckCommon(d, port, b, r.error, false, w);
  }
}

void FuzzSetting(Driver& d, FuzzPort& port, Input& in, World& w, uint8_t op) {
  const Before b = Snapshot(d, port);
  const bool clean_before = w.clean;
  const uint8_t sub = in.Byte();
  Error e = Error::Ok;
  bool writes = true;
  if (op == 8) {
    e = d.SetRange(static_cast<ads7952::Range>(sub % 3U)).error;
  } else if (op == 9) {
    e = d.SetAuto1Channels(in.Word(), (sub & 1U) != 0U ? w.verify : Verify::None).error;
  } else if (sub % 4U == 0U) {
    e = d.SetAuto2LastChannel(static_cast<uint8_t>(in.Byte() % 17U), w.verify).error;
  } else if (sub % 4U == 1U) {
    const uint8_t ch = in.Byte() % 17U;
    e = d.SetAlarmThresholds(ch, {static_cast<uint16_t>(in.Word() % 4200U),
                                  static_cast<uint16_t>(in.Word() % 4200U)})
            .error;
  } else if (sub % 4U == 2U) {
    e = d.SetGpioOutputs(static_cast<uint8_t>(in.Byte() & 0x1FU), w.verify).error;
  } else {
    const auto r = d.ReadGpio();
    Check(r.ok() || r.value == 0);
    e = r.error;
    writes = false;
  }
  const bool trusted = CheckCommon(d, port, b, e, false, w);
  if (writes && b.up && e == Error::Bus) {
    Check(d.IsConfigUncertain()); // a failed write may have taken effect
  }
  if (e == Error::WriteMismatch) {
    // Only a board fault or a bus fault makes a read-back differ.
    Check(!trusted || !clean_before || port.Chip().gpio_held != 0);
  }
  CheckRegisters(d, port, w);
}

void RunOne(Driver& d, FuzzPort& port, Input& in, World& w) {
  const uint8_t op = in.Byte() % 12U;
  if (op <= 2) {
    FuzzLifecycle(d, port, in, w, op);
  } else if (op <= 7) {
    FuzzConversion(d, port, in, w, op);
  } else if (op <= 10) {
    FuzzSetting(d, port, in, w, op);
  } else {
    FuzzBoard(port, in, w);
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzPort port(in);
  World world{};
  const uint8_t setup = in.Byte();
  port.Chip().variant = static_cast<ads7952_test::chip::Part>(setup % 4U);
  port.Chip().package = (setup & 0x04U) != 0U ? ads7952_test::chip::Package::Vqfn
                                              : ads7952_test::chip::Package::Tssop;
  port.Chip().Brownout(); // the registers of the chosen part
  Driver driver(port, FuzzConfig(in, port.Chip(), world));
  for (int calls = 0; calls < 64 && !in.Empty(); ++calls) {
    RunOne(driver, port, in, world);
  }
  return 0;
}
