cmake_minimum_required(VERSION 3.20)
project(JOJOPsxR3000aFoundationTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(jojo_psx_r3000a_foundation_tests main.cpp)
target_include_directories(jojo_psx_r3000a_foundation_tests PRIVATE ../../src)

if(MSVC)
  target_compile_options(jojo_psx_r3000a_foundation_tests PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(jojo_psx_r3000a_foundation_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
