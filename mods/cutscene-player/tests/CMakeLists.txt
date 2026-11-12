# Tests of the player's JSON reader and timeline sums; they need no SDK and no game:
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
cmake_minimum_required(VERSION 3.20)
project(cutscene_player_tests LANGUAGES CXX)
enable_testing()

add_executable(timeline_test timeline_test.cpp)
target_include_directories(timeline_test PRIVATE ../src)
target_compile_features(timeline_test PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(timeline_test PRIVATE /W4)
else()
  target_compile_options(timeline_test PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all)
  target_link_options(timeline_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME timeline COMMAND timeline_test)
