/**
 * @file ads7952_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_ADS7952_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-ads7952 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the reset state of the chosen part, so
 * `KconfigDefaults()` equals `Config{}`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so an absent
 * `CONFIG_ADS7952_RANGE_2VREF` means Range 1 in both cases. GPIO and alarm settings are not
 * Kconfig options: set them in the `Config` in code.
 *
 * One configuration per program: the inline function below depends on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h`
 * per build; each host test is its own program). No driver header includes this one (handbook
 * KB-CPP20-018).
 */
#ifndef ADS7952_KCONFIG_HPP
#define ADS7952_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "ads7952_types.hpp"

#ifndef CONFIG_ADS7952_VREF_MV
/** REFP in millivolts when Kconfig does not set it: `Config{}.vref_mv`. */
#define CONFIG_ADS7952_VREF_MV 2500
#endif
#ifndef CONFIG_ADS7952_VA_MIN_MV
/** Lowest guaranteed +VA in millivolts when Kconfig does not set it: `Config{}.va_min_mv`. */
#define CONFIG_ADS7952_VA_MIN_MV 5000
#endif

namespace ads7952 {

/**
 * @brief The configuration menuconfig selected, on top of the chosen part's reset state.
 * @return `ResetConfig(variant)` with the package, range, voltages and sequences from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
#ifdef CONFIG_ADS7952_VARIANT_ADS7950
  Config c = ResetConfig(Variant::Ads7950);
#elif defined(CONFIG_ADS7952_VARIANT_ADS7951)
  Config c = ResetConfig(Variant::Ads7951);
#elif defined(CONFIG_ADS7952_VARIANT_ADS7953)
  Config c = ResetConfig(Variant::Ads7953);
#else
  Config c = ResetConfig(Variant::Ads7952);
#endif
#if defined(CONFIG_ADS7952_PACKAGE_VQFN) && CONFIG_ADS7952_PACKAGE_VQFN
  c.package = Package::Vqfn;
#endif
#if defined(CONFIG_ADS7952_RANGE_2VREF) && CONFIG_ADS7952_RANGE_2VREF
  c.range = Range::TwoVref;
#endif
  c.vref_mv = CONFIG_ADS7952_VREF_MV;
  c.va_min_mv = CONFIG_ADS7952_VA_MIN_MV;
#ifdef CONFIG_ADS7952_AUTO1_CHANNELS
  c.auto1_channels = CONFIG_ADS7952_AUTO1_CHANNELS;
#endif
#ifdef CONFIG_ADS7952_AUTO2_LAST_CHANNEL
  c.auto2_last_channel = CONFIG_ADS7952_AUTO2_LAST_CHANNEL;
#endif
  return c;
}

} // namespace ads7952

#endif // ADS7952_KCONFIG_HPP
