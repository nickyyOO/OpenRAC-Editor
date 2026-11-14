# OpenRAC behaviour presets: an official mod of OpenRAC Editor that plays the simple behaviours
# (spin, bob, patrol, orbit, face the hero, follow) the editor packs with a level.
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja                   # or -DOPENRAC_SDK=<OpenRAC checkout>/sdk
#   cmake --build build
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/behaviour_presets
cmake_minimum_required(VERSION 3.20)
project(behaviour_presets LANGUAGES C CXX)

if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.0 REQUIRED)

openrac_add_mod(behaviour_presets MANIFEST openrac-mod.json SOURCES src/behaviours.cpp)
target_compile_features(behaviour_presets PRIVATE cxx_std_20)
if(NOT MSVC)
  target_compile_options(behaviour_presets PRIVATE -Wall -Wextra)
endif()
