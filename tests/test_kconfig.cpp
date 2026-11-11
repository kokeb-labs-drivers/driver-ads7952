/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "ads7952_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr ads7952::Config kC = ads7952::KconfigDefaults();
  CHECK_EQ(kC.variant, ads7952::Variant::Ads7951);
  CHECK_EQ(kC.package, ads7952::Package::Vqfn);
  CHECK_EQ(kC.range, ads7952::Range::TwoVref);
  CHECK_EQ(kC.vref_mv, 2048U);
  CHECK_EQ(kC.va_min_mv, 4500U);
  CHECK_EQ(kC.auto1_channels, 0x00A5U);
  CHECK_EQ(kC.auto2_last_channel, 5U);
  CHECK_EQ(ads7952::Validate(kC), ads7952::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr ads7952::Config kC = ads7952::KconfigDefaults();
  constexpr ads7952::Config kD{};
  CHECK_EQ(kC.variant, kD.variant);
  CHECK_EQ(kC.package, kD.package);
  CHECK_EQ(kC.range, kD.range);
  CHECK_EQ(kC.vref_mv, kD.vref_mv);
  CHECK_EQ(kC.va_min_mv, kD.va_min_mv);
  CHECK_EQ(kC.auto1_channels, kD.auto1_channels);
  CHECK_EQ(kC.auto2_last_channel, kD.auto2_last_channel);
}
#endif
