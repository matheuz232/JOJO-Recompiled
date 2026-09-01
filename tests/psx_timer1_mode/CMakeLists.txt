cmake_minimum_required(VERSION 3.20)
project(JOJOTimer1ModeTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(jojo_psx_timer1_mode_tests
  main.cpp
  timer02_contract.cpp
  bios_timer_contract.cpp)
target_include_directories(jojo_psx_timer1_mode_tests PRIVATE ../../src)

if(MSVC)
  target_compile_options(jojo_psx_timer1_mode_tests PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(jojo_psx_timer1_mode_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
