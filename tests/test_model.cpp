/**
 * @file test_model.cpp
 * @brief The chip model's frame format against the driver's, at compile time (handbook KB-TST-030).
 *
 * tests/mock_ads7952.hpp is written from the datasheet without the driver's headers, so a constant
 * the driver gets wrong is not copied into the model. These checks make the two agree, or the build
 * fails: a difference is a mistake in one of them, found against the datasheet section the model
 * names.
 */
#include <cstdint>

#include "ads7952.hpp"
#include "mock_ads7952.hpp"
#include "test_harness.hpp"

namespace {

namespace chip = ads7952_test::chip;
namespace reg = ads7952::reg;
using ads7952::Package;
using ads7952::Variant;

// --8<-- [start:cross-check]
// Command words, DI15:12 (Tables 1, 2, 3, 5, 6, 11, 13).
static_assert(chip::kCommandShift == reg::kCommandShift && chip::kContinue == reg::kContinue &&
              chip::kManual == reg::kManual && chip::kAuto1 == reg::kAuto1 &&
              chip::kAuto2 == reg::kAuto2 && chip::kGpioProgram == reg::kGpioProgram &&
              chip::kAuto1Program == reg::kAuto1Program &&
              chip::kAuto2Program == reg::kAuto2Program &&
              chip::kAlarmProgramGroup0 == reg::kAlarmProgramGroup0);
// Mode control word (Tables 1, 2, 5).
static_assert(chip::kProgramEnable == reg::kProgramEnable && chip::kAutoReset == reg::kAutoReset &&
              chip::kManualChannelShift == reg::kManualChannelShift &&
              chip::kRangeTwoVref == reg::kRangeTwoVref && chip::kPowerDown == reg::kPowerDown &&
              chip::kGpioReadout == reg::kGpioReadout && chip::kGpioDataMask == reg::kGpioDataMask);
// Program registers (Tables 6, 11, 13) and their reset values (section 8.5.3).
static_assert(chip::kAuto2LastShift == reg::kAuto2LastShift &&
              chip::kGpioResetAll == reg::kGpioResetAll &&
              chip::kGpio3PowerDown == reg::kGpio3PowerDown &&
              chip::kGpio2Range == reg::kGpio2Range &&
              chip::kGpioAlarmShift == reg::kGpioAlarmShift);
static_assert(chip::kAlarmChannelShift == reg::kAlarmChannelShift &&
              chip::kAlarmHigh == reg::kAlarmHigh && chip::kAlarmExit == reg::kAlarmExit &&
              chip::kAlarmThresholdMask == reg::kAlarmThresholdMask &&
              chip::kAlarmHighReset == reg::kAlarmHighReset &&
              chip::kAlarmLowReset == reg::kAlarmLowReset);
// Reply word and frame (section 8.1), timing (section 7.6).
static_assert(chip::kReplyAddressShift == reg::kReplyAddressShift &&
              chip::kReplyCodeMask == reg::kReplyCodeMask &&
              chip::kFrameBytes == ads7952::kFrameBytes && chip::kPowerUpUs == ads7952::kPowerUpUs);
// Parts, packages and channel counts (Table 4, section 8.5.2).
static_assert(chip::ChannelCount(chip::Part::Ads7950) == ads7952::ChannelCount(Variant::Ads7950) &&
              chip::ChannelCount(chip::Part::Ads7951) == ads7952::ChannelCount(Variant::Ads7951) &&
              chip::ChannelCount(chip::Part::Ads7952) == ads7952::ChannelCount(Variant::Ads7952) &&
              chip::ChannelCount(chip::Part::Ads7953) == ads7952::ChannelCount(Variant::Ads7953));
static_assert(ads7952_test::ModelPart(Variant::Ads7953) == chip::Part::Ads7953 &&
              ads7952_test::DriverVariant(chip::Part::Ads7950) == Variant::Ads7950);
static_assert(ads7952_test::ModelPackage(Package::Vqfn) == chip::Package::Vqfn &&
              ads7952_test::DriverPackage(chip::Package::Tssop) == Package::Tssop);
// --8<-- [end:cross-check]

} // namespace

TEST_CASE("The model resets to the driver's reset configuration of every part") {
  for (uint8_t p = 0; p < 4U; ++p) {
    ads7952_test::Ads7952Model model;
    model.variant = static_cast<chip::Part>(p);
    model.Brownout();
    const ads7952::Config reset = ads7952::ResetConfig(ads7952_test::DriverVariant(model.variant));
    CHECK_EQ(model.auto1_mask, reset.auto1_channels);
    CHECK_EQ(model.auto2_last, reset.auto2_last_channel);
    CHECK_EQ(model.Channels(), ads7952::ChannelCount(reset.variant));
  }
}
