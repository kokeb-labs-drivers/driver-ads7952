#===============================================================================
# ADS7952 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(ADS7952_TARGET_NAME "ads7952")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(ADS7952_VERSION_MAJOR 0)
set(ADS7952_VERSION_MINOR 1)
set(ADS7952_VERSION_PATCH 0)
set(ADS7952_VERSION "${ADS7952_VERSION_MAJOR}.${ADS7952_VERSION_MINOR}.${ADS7952_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(ADS7952_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/ads7952_version.h.in")
set(ADS7952_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/ads7952_generated")
set(ADS7952_VERSION_HEADER "${ADS7952_VERSION_HEADER_DIR}/ads7952_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${ADS7952_VERSION_HEADER_DIR}")
configure_file("${ADS7952_VERSION_TEMPLATE}" "${ADS7952_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(ADS7952_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${ADS7952_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/ads7952.ipp, included by inc/ads7952.hpp.
set(ADS7952_SOURCE_FILES)

set(ADS7952_IDF_REQUIRES)
