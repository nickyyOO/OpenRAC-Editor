# The OpenRAC cutscene player: an official mod of OpenRAC Editor that plays the .cutscene files
# the editor packs with a level (docs/CUTSCENE_FORMAT.md in the editor's repository).
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja                   # or -DOPENRAC_SDK=<OpenRAC checkout>/sdk
#   cmake --build build
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/cutscene_player
cmake_minimum_required(VERSION 3.20)
project(cutscene_player LANGUAGES C CXX)

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

openrac_add_mod(cutscene_player MANIFEST openrac-mod.json SOURCES src/player.cpp)
target_compile_features(cutscene_player PRIVATE cxx_std_20)
if(NOT MSVC)
  target_compile_options(cutscene_player PRIVATE -Wall -Wextra)
endif()
