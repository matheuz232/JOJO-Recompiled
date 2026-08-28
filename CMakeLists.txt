cmake_minimum_required(VERSION 3.20)
project(JOJORecompiled VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_library(jojo_core STATIC
  src/core/version.cpp
  src/core/settings.cpp
  src/core/input.cpp
  src/core/disc_image.cpp
  src/core/disc_media.cpp
  src/core/iso9660.cpp
  src/core/dreamcast_boot.cpp
  src/core/dreamcast_analysis.cpp
  src/core/sh4_decoder.cpp
  src/core/sh4_cfg.cpp
  src/core/revision.cpp
  src/core/conversion.cpp
  src/core/runtime.cpp
  src/core/device_id.cpp
)
target_include_directories(jojo_core PUBLIC src)

# The shipping Windows application is intentionally a single executable.
# Developer tests remain separate build artifacts and are never part of the end-user package.
if(WIN32)
  add_executable(jojo_recompiled WIN32
    src/app_win32/main.cpp
  )
  set_target_properties(jojo_recompiled PROPERTIES OUTPUT_NAME "JOJO-Recompiled")
  target_compile_definitions(jojo_recompiled PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
  )
  target_link_libraries(jojo_recompiled PRIVATE jojo_core shell32 ole32 uuid)
endif()

add_executable(jojo_tests tests/test_main.cpp)
target_link_libraries(jojo_tests PRIVATE jojo_core)
add_test(NAME jojo_tests COMMAND jojo_tests)

add_executable(jojo_iso_tests tests/test_iso9660.cpp)
target_link_libraries(jojo_iso_tests PRIVATE jojo_core)
add_test(NAME jojo_iso_tests COMMAND jojo_iso_tests)

add_executable(jojo_media_tests tests/test_disc_media.cpp)
target_link_libraries(jojo_media_tests PRIVATE jojo_core)
add_test(NAME jojo_media_tests COMMAND jojo_media_tests)

add_executable(jojo_boot_tests tests/test_dreamcast_boot.cpp)
target_link_libraries(jojo_boot_tests PRIVATE jojo_core)
add_test(NAME jojo_boot_tests COMMAND jojo_boot_tests)

add_executable(jojo_sh4_tests tests/test_sh4_decoder.cpp)
target_link_libraries(jojo_sh4_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_tests COMMAND jojo_sh4_tests)

add_executable(jojo_sh4_cfg_tests tests/test_sh4_cfg.cpp)
target_link_libraries(jojo_sh4_cfg_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_cfg_tests COMMAND jojo_sh4_cfg_tests)

add_executable(jojo_analysis_tests tests/test_dreamcast_analysis.cpp)
target_link_libraries(jojo_analysis_tests PRIVATE jojo_core)
add_test(NAME jojo_analysis_tests COMMAND jojo_analysis_tests)
