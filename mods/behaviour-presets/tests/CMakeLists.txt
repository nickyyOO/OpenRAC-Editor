# Tests of the presets' sums and the JSON reader; they need no SDK and no game:
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
cmake_minimum_required(VERSION 3.20)
project(behaviour_presets_tests LANGUAGES CXX)
enable_testing()

add_executable(presets_test presets_test.cpp)
target_include_directories(presets_test PRIVATE ../src)
target_compile_features(presets_test PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(presets_test PRIVATE /W4)
else()
  target_compile_options(presets_test PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all)
  target_link_options(presets_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME presets COMMAND presets_test)
