# Tests of editor-runtime's sums: the JSON reader, the cutscene timeline and the behaviour presets.
# They need no SDK and no game:
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
cmake_minimum_required(VERSION 3.20)
project(editor_runtime_tests LANGUAGES CXX)
enable_testing()

foreach(test timeline presets)
  add_executable(${test}_test ${test}_test.cpp)
  target_include_directories(${test}_test PRIVATE ../src)
  target_compile_features(${test}_test PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(${test}_test PRIVATE /W4)
  else()
    target_compile_options(${test}_test PRIVATE -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(${test}_test PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ${test} COMMAND ${test}_test)
endforeach()
