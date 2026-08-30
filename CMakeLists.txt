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
  src/core/presentation.cpp
  src/core/input.cpp
  src/core/settings_menu.cpp
  src/core/training.cpp
  src/core/rollback.cpp
  src/core/network_protocol.cpp
  src/core/online.cpp
  src/core/replay.cpp
  src/core/disc_image.cpp
  src/core/disc_media.cpp
  src/core/iso9660.cpp
  src/core/dreamcast_boot.cpp
  src/core/dreamcast_analysis.cpp
  src/core/dreamcast_memory.cpp
  src/core/dreamcast_bus.cpp
  src/core/dreamcast_system_asic.cpp
  src/core/dreamcast_interrupts.cpp
  src/core/dreamcast_pvr2.cpp
  src/core/dreamcast_boot_runner.cpp
  src/core/sh4_decoder.cpp
  src/core/sh4_opcode_census.cpp
  src/core/sh4_cfg.cpp
  src/core/sh4_ir.cpp
  src/core/sh4_reference_executor.cpp
  src/core/sh4_reference_boundary.cpp
  src/core/sh4_reference_bus.cpp
  src/core/sh4_interrupt_entry.cpp
  src/core/revision.cpp
  src/core/conversion.cpp
  src/core/runtime.cpp
  src/core/native_backend.cpp
  src/core/native_x64.cpp
  src/core/device_id.cpp
  src/core/semver.cpp
  src/core/sha256.cpp
  src/core/mod_runtime.cpp
  src/core/mod_resolver.cpp
  src/core/mod_content.cpp
  src/core/mod_policy.cpp
  src/core/native_mod_loader.cpp
)
target_include_directories(jojo_core PUBLIC src)
if(CMAKE_DL_LIBS)
  target_link_libraries(jojo_core PUBLIC ${CMAKE_DL_LIBS})
endif()

if(WIN32)
  add_library(jojo_win32_input_host STATIC
    src/platform/windows/controller_win32.cpp)
  target_compile_definitions(jojo_win32_input_host PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_input_host PUBLIC jojo_core setupapi hid user32)

  add_executable(jojo_recompiled WIN32
    src/app_win32/main.cpp
    src/app_win32/presentation_host.cpp)
  set_target_properties(jojo_recompiled PROPERTIES OUTPUT_NAME "JOJO-Recompiled")
  target_compile_definitions(jojo_recompiled PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_recompiled PRIVATE jojo_core jojo_win32_input_host shell32 ole32 uuid d3d11 dxgi)

  add_executable(jojo_win32_presentation_tests
    tests/test_win32_presentation.cpp
    src/app_win32/presentation_host.cpp)
  target_compile_definitions(jojo_win32_presentation_tests PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_presentation_tests PRIVATE jojo_core d3d11 dxgi)
  add_test(NAME jojo_win32_presentation_tests COMMAND jojo_win32_presentation_tests)

  add_executable(jojo_win32_input_tests tests/test_win32_input.cpp)
  target_compile_definitions(jojo_win32_input_tests PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_input_tests PRIVATE jojo_win32_input_host)
  add_test(NAME jojo_win32_input_tests COMMAND jojo_win32_input_tests)
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
add_executable(jojo_sh4_ir_tests tests/test_sh4_ir.cpp)
target_link_libraries(jojo_sh4_ir_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_ir_tests COMMAND jojo_sh4_ir_tests)
add_executable(jojo_sh4_reference_tests tests/test_sh4_reference_executor.cpp)
target_link_libraries(jojo_sh4_reference_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_reference_tests COMMAND jojo_sh4_reference_tests)
add_executable(jojo_sh4_integer_pipeline_tests tests/test_sh4_integer_pipeline.cpp)
target_link_libraries(jojo_sh4_integer_pipeline_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_integer_pipeline_tests COMMAND jojo_sh4_integer_pipeline_tests)
add_executable(jojo_sh4_memory_pipeline_tests tests/test_sh4_memory_pipeline.cpp)
target_link_libraries(jojo_sh4_memory_pipeline_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_memory_pipeline_tests COMMAND jojo_sh4_memory_pipeline_tests)
add_executable(jojo_sh4_addressing_pipeline_tests tests/test_sh4_addressing_pipeline.cpp)
target_link_libraries(jojo_sh4_addressing_pipeline_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_addressing_pipeline_tests COMMAND jojo_sh4_addressing_pipeline_tests)
add_executable(jojo_sh4_gbr_memory_tests tests/test_sh4_gbr_memory.cpp)
target_link_libraries(jojo_sh4_gbr_memory_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_gbr_memory_tests COMMAND jojo_sh4_gbr_memory_tests)
add_executable(jojo_sh4_gbr_register_transfer_tests tests/test_sh4_gbr_register_transfer.cpp)
target_link_libraries(jojo_sh4_gbr_register_transfer_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_gbr_register_transfer_tests COMMAND jojo_sh4_gbr_register_transfer_tests)
add_executable(jojo_sh4_pr_mac_register_transfer_tests tests/test_sh4_pr_mac_register_transfer.cpp)
target_link_libraries(jojo_sh4_pr_mac_register_transfer_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_pr_mac_register_transfer_tests COMMAND jojo_sh4_pr_mac_register_transfer_tests)
add_executable(jojo_sh4_multiply_tests tests/test_sh4_multiply.cpp)
target_link_libraries(jojo_sh4_multiply_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_multiply_tests COMMAND jojo_sh4_multiply_tests)
add_executable(jojo_sh4_data_manipulation_tests tests/test_sh4_data_manipulation.cpp)
target_link_libraries(jojo_sh4_data_manipulation_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_data_manipulation_tests COMMAND jojo_sh4_data_manipulation_tests)
add_executable(jojo_sh4_carry_overflow_tests tests/test_sh4_carry_overflow.cpp)
target_link_libraries(jojo_sh4_carry_overflow_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_carry_overflow_tests COMMAND jojo_sh4_carry_overflow_tests)
add_executable(jojo_sh4_opcode_census_tests tests/test_sh4_opcode_census.cpp)
target_link_libraries(jojo_sh4_opcode_census_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_opcode_census_tests COMMAND jojo_sh4_opcode_census_tests)
add_executable(jojo_dreamcast_executable_memory_tests tests/test_dreamcast_executable_memory.cpp)
target_link_libraries(jojo_dreamcast_executable_memory_tests PRIVATE jojo_core)
add_test(NAME jojo_dreamcast_executable_memory_tests COMMAND jojo_dreamcast_executable_memory_tests)
add_executable(jojo_dreamcast_boot_runner_tests tests/test_dreamcast_boot_runner.cpp)
target_link_libraries(jojo_dreamcast_boot_runner_tests PRIVATE jojo_core)
add_test(NAME jojo_dreamcast_boot_runner_tests COMMAND jojo_dreamcast_boot_runner_tests)
add_executable(jojo_sh4_memory_bus_tests tests/test_sh4_memory_bus.cpp)
target_link_libraries(jojo_sh4_memory_bus_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_memory_bus_tests COMMAND jojo_sh4_memory_bus_tests)
add_executable(jojo_dreamcast_bus_diagnostics_tests tests/test_dreamcast_bus_diagnostics.cpp)
target_link_libraries(jojo_dreamcast_bus_diagnostics_tests PRIVATE jojo_core)
add_test(NAME jojo_dreamcast_bus_diagnostics_tests COMMAND jojo_dreamcast_bus_diagnostics_tests)
add_executable(jojo_dreamcast_system_asic_tests tests/test_dreamcast_system_asic.cpp)
target_link_libraries(jojo_dreamcast_system_asic_tests PRIVATE jojo_core)
add_test(NAME jojo_dreamcast_system_asic_tests COMMAND jojo_dreamcast_system_asic_tests)
add_executable(jojo_sh4_interrupt_entry_tests tests/test_sh4_interrupt_entry.cpp)
target_link_libraries(jojo_sh4_interrupt_entry_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_interrupt_entry_tests COMMAND jojo_sh4_interrupt_entry_tests)
add_executable(jojo_dreamcast_irq_delivery_tests tests/test_dreamcast_irq_delivery.cpp)
target_link_libraries(jojo_dreamcast_irq_delivery_tests PRIVATE jojo_core)
add_test(NAME jojo_dreamcast_irq_delivery_tests COMMAND jojo_dreamcast_irq_delivery_tests)
add_executable(jojo_sh4_block_boundary_tests tests/test_sh4_block_boundary.cpp)
target_link_libraries(jojo_sh4_block_boundary_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_block_boundary_tests COMMAND jojo_sh4_block_boundary_tests)
add_executable(jojo_dreamcast_pvr2_vblank_tests tests/test_dreamcast_pvr2_vblank.cpp)
target_link_libraries(jojo_dreamcast_pvr2_vblank_tests PRIVATE jojo_core)
add_test(NAME jojo_dreamcast_pvr2_vblank_tests COMMAND jojo_dreamcast_pvr2_vblank_tests)
add_executable(jojo_sh4_fpu_constant_tests tests/test_sh4_fpu_constants.cpp)
target_link_libraries(jojo_sh4_fpu_constant_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_constant_tests COMMAND jojo_sh4_fpu_constant_tests)
add_executable(jojo_sh4_fpul_transfer_tests tests/test_sh4_fpul_transfers.cpp)
target_link_libraries(jojo_sh4_fpul_transfer_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpul_transfer_tests COMMAND jojo_sh4_fpul_transfer_tests)
add_executable(jojo_sh4_fpscr_bank_tests tests/test_sh4_fpscr_banks.cpp)
target_link_libraries(jojo_sh4_fpscr_bank_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpscr_bank_tests COMMAND jojo_sh4_fpscr_bank_tests)
add_executable(jojo_sh4_flds_fsts_tests tests/test_sh4_flds_fsts.cpp)
target_link_libraries(jojo_sh4_flds_fsts_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_flds_fsts_tests COMMAND jojo_sh4_flds_fsts_tests)
add_executable(jojo_sh4_fpu_mode_switch_tests tests/test_sh4_fpu_mode_switches.cpp)
target_link_libraries(jojo_sh4_fpu_mode_switch_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_mode_switch_tests COMMAND jojo_sh4_fpu_mode_switch_tests)
add_executable(jojo_sh4_float_ftrc_tests tests/test_sh4_float_ftrc.cpp)
target_link_libraries(jojo_sh4_float_ftrc_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_float_ftrc_tests COMMAND jojo_sh4_float_ftrc_tests)
add_executable(jojo_sh4_fmov_register_tests tests/test_sh4_fmov_registers.cpp)
target_link_libraries(jojo_sh4_fmov_register_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fmov_register_tests COMMAND jojo_sh4_fmov_register_tests)
add_executable(jojo_sh4_fmov_memory_tests tests/test_sh4_fmov_memory.cpp)
target_link_libraries(jojo_sh4_fmov_memory_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fmov_memory_tests COMMAND jojo_sh4_fmov_memory_tests)
add_executable(jojo_sh4_fpu_arithmetic_tests tests/test_sh4_fpu_arithmetic.cpp)
target_link_libraries(jojo_sh4_fpu_arithmetic_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_arithmetic_tests COMMAND jojo_sh4_fpu_arithmetic_tests)
add_executable(jojo_sh4_fpu_compare_tests tests/test_sh4_fpu_compare.cpp)
target_link_libraries(jojo_sh4_fpu_compare_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_compare_tests COMMAND jojo_sh4_fpu_compare_tests)
add_executable(jojo_sh4_fpu_unary_tests tests/test_sh4_fpu_unary.cpp)
target_link_libraries(jojo_sh4_fpu_unary_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_unary_tests COMMAND jojo_sh4_fpu_unary_tests)
add_executable(jojo_sh4_fpu_completion_tests tests/test_sh4_fpu_completion.cpp)
target_link_libraries(jojo_sh4_fpu_completion_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_completion_tests COMMAND jojo_sh4_fpu_completion_tests)

add_executable(jojo_sh4_fpu_disable_tests tests/test_sh4_fpu_disable.cpp)
target_link_libraries(jojo_sh4_fpu_disable_tests PRIVATE jojo_core)
add_test(NAME jojo_sh4_fpu_disable_tests COMMAND jojo_sh4_fpu_disable_tests)

add_executable(jojo_native_backend_tests tests/test_native_backend.cpp)
target_link_libraries(jojo_native_backend_tests PRIVATE jojo_core)
add_test(NAME jojo_native_backend_tests COMMAND jojo_native_backend_tests)

add_executable(jojo_presentation_tests tests/test_presentation.cpp)
target_link_libraries(jojo_presentation_tests PRIVATE jojo_core)
add_test(NAME jojo_presentation_tests COMMAND jojo_presentation_tests)

add_executable(jojo_semver_tests tests/test_semver.cpp)
target_link_libraries(jojo_semver_tests PRIVATE jojo_core)
add_test(NAME jojo_semver_tests COMMAND jojo_semver_tests)

add_executable(jojo_sha256_tests tests/test_sha256.cpp)
target_link_libraries(jojo_sha256_tests PRIVATE jojo_core)
add_test(NAME jojo_sha256_tests COMMAND jojo_sha256_tests)

add_executable(jojo_mod_runtime_tests tests/test_mod_runtime.cpp)
target_link_libraries(jojo_mod_runtime_tests PRIVATE jojo_core)
add_test(NAME jojo_mod_runtime_tests COMMAND jojo_mod_runtime_tests)

add_executable(jojo_m5_settings_input_tests tests/test_m5_settings_input.cpp)
target_link_libraries(jojo_m5_settings_input_tests PRIVATE jojo_core)
add_test(NAME jojo_m5_settings_input_tests COMMAND jojo_m5_settings_input_tests)

add_executable(jojo_training_tests tests/test_training.cpp)
target_link_libraries(jojo_training_tests PRIVATE jojo_core)
add_test(NAME jojo_training_tests COMMAND jojo_training_tests)

add_executable(jojo_rollback_tests tests/test_rollback.cpp)
target_link_libraries(jojo_rollback_tests PRIVATE jojo_core)
add_test(NAME jojo_rollback_tests COMMAND jojo_rollback_tests)

add_executable(jojo_online_tests tests/test_online.cpp)
target_link_libraries(jojo_online_tests PRIVATE jojo_core)
add_test(NAME jojo_online_tests COMMAND jojo_online_tests)

add_executable(jojo_online_hardening_tests tests/test_online_hardening.cpp)
target_link_libraries(jojo_online_hardening_tests PRIVATE jojo_core)
add_test(NAME jojo_online_hardening_tests COMMAND jojo_online_hardening_tests)

add_executable(jojo_mod_policy_tests tests/test_mod_policy.cpp)
target_link_libraries(jojo_mod_policy_tests PRIVATE jojo_core)
add_test(NAME jojo_mod_policy_tests COMMAND jojo_mod_policy_tests)

function(add_jojo_native_mod_fixture target mod_id)
  add_library(${target} SHARED tests/fixtures/test_native_mod.cpp)
  target_include_directories(${target} PRIVATE src)
  target_compile_definitions(${target} PRIVATE JOJO_TEST_NATIVE_MOD_ID=\"${mod_id}\")
endfunction()

add_jojo_native_mod_fixture(jojo_native_mod_fixture_first native.first)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_second native.second)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_failure native.failure)
target_compile_definitions(jojo_native_mod_fixture_failure PRIVATE JOJO_TEST_NATIVE_MOD_LOAD_RESULT=7)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_bad_abi native.badabi)
target_compile_definitions(jojo_native_mod_fixture_bad_abi PRIVATE JOJO_TEST_NATIVE_MOD_ABI=2)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_no_export native.noexport)
target_compile_definitions(jojo_native_mod_fixture_no_export PRIVATE JOJO_TEST_NATIVE_MOD_NO_EXPORT=1)

add_executable(jojo_native_mod_loader_tests tests/test_native_mod_loader.cpp)
target_link_libraries(jojo_native_mod_loader_tests PRIVATE jojo_core)
add_dependencies(jojo_native_mod_loader_tests
  jojo_native_mod_fixture_first
  jojo_native_mod_fixture_second
  jojo_native_mod_fixture_failure
  jojo_native_mod_fixture_bad_abi
  jojo_native_mod_fixture_no_export)
add_test(
  NAME jojo_native_mod_loader_tests
  COMMAND jojo_native_mod_loader_tests
    "$<TARGET_FILE:jojo_native_mod_fixture_first>"
    "$<TARGET_FILE:jojo_native_mod_fixture_second>"
    "$<TARGET_FILE:jojo_native_mod_fixture_failure>"
    "$<TARGET_FILE:jojo_native_mod_fixture_bad_abi>"
    "$<TARGET_FILE:jojo_native_mod_fixture_no_export>")