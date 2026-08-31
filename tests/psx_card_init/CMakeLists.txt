cmake_minimum_required(VERSION 3.20)
project(jojo_psx_card_init_tests LANGUAGES CXX)

add_executable(jojo_psx_card_init_tests main.cpp)
target_compile_features(jojo_psx_card_init_tests PRIVATE cxx_std_20)
target_include_directories(jojo_psx_card_init_tests PRIVATE ../../src)

if(MSVC)
  target_compile_options(jojo_psx_card_init_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(jojo_psx_card_init_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
