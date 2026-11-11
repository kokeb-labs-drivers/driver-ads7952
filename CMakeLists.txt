#===============================================================================
# ADS7952 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   ads7952, consumed as ads7952::ads7952 both through add_subdirectory() and
#   through find_package(ads7952) after install.
#
#   cmake -S . -B build -DADS7952_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/ads7952_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${ADS7952_SOURCE_FILES}
        INCLUDE_DIRS ${ADS7952_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${ADS7952_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(ADS7952_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(ads7952_driver
    VERSION ${ADS7952_VERSION}
    DESCRIPTION "C++20 driver for the TI ADS7950, ADS7951, ADS7952 and ADS7953 12-bit SPI SAR ADCs."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because ads7952.hpp includes
# "../src/ads7952.ipp" relative to itself:
#   <prefix>/include/ads7952/inc/*.hpp, ads7952_version.h
#   <prefix>/include/ads7952/src/ads7952.ipp
set(ADS7952_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/ads7952/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${ADS7952_TARGET_NAME} INTERFACE)
target_include_directories(${ADS7952_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${ADS7952_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${ADS7952_INSTALL_INCLUDEDIR}>
)
target_compile_features(${ADS7952_TARGET_NAME} INTERFACE cxx_std_20)

# ads7952::ads7952 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(ads7952::ads7952 ALIAS ${ADS7952_TARGET_NAME})
set_target_properties(${ADS7952_TARGET_NAME} PROPERTIES EXPORT_NAME ads7952)

option(ADS7952_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links ads7952::ads7952" OFF)
if(ADS7952_ENABLE_WARNINGS)
    target_compile_options(${ADS7952_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(ADS7952_BUILD_TESTS "Build the host tests in tests/" OFF)
option(ADS7952_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(ADS7952_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${ADS7952_TARGET_NAME} EXPORT ads7952Targets
    INCLUDES DESTINATION ${ADS7952_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${ADS7952_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${ADS7952_VERSION_HEADER}" DESTINATION ${ADS7952_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ads7952/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT ads7952Targets
    FILE ads7952Targets.cmake
    NAMESPACE ads7952::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ads7952)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/ads7952Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ads7952Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ads7952)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ads7952ConfigVersion.cmake"
    VERSION ${ADS7952_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ads7952Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ads7952ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ads7952)
