cmake_minimum_required(VERSION 3.20)
project(jojo_r3000a_boot_slice LANGUAGES CXX)

add_executable(jojo_r3000a_boot_slice_tests ../test_psx_r3000a_boot_slice.cpp)
target_compile_features(jojo_r3000a_boot_slice_tests PRIVATE cxx_std_20)
target_include_directories(jojo_r3000a_boot_slice_tests PRIVATE ../../src)

if(MSVC)
    target_compile_options(jojo_r3000a_boot_slice_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(jojo_r3000a_boot_slice_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME jojo_r3000a_boot_slice_tests COMMAND jojo_r3000a_boot_slice_tests)
