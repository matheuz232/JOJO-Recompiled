#include "core/psx_runtime.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t pack_sxy(std::int16_t x, std::int16_t y) {
    return static_cast<std::uint16_t>(x) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16u);
}

static std::uint32_t gte_math_instruction(std::uint8_t command, bool sf, bool lm) {
    return 0x4a000000u |
           (sf ? (1u << 19u) : 0u) |
           (lm ? (1u << 10u) : 0u) |
           command;
}

static std::uint32_t mvmva_instruction(bool sf,
                                       std::uint8_t mx,
                                       std::uint8_t v,
                                       std::uint8_t cv,
                                       bool lm) {
    return 0x4a000012u |
           (sf ? (1u << 19u) : 0u) |
           ((static_cast<std::uint32_t>(mx) & 3u) << 17u) |
           ((static_cast<std::uint32_t>(v) & 3u) << 15u) |
           ((static_cast<std::uint32_t>(cv) & 3u) << 13u) |
           (lm ? (1u << 10u) : 0u);
}

static void set_identity_matrix(jojo::PsxGteState& gte, std::uint8_t base) {
    gte.control[base + 0u] = 0x00001000u;
    gte.control[base + 1u] = 0u;
    gte.control[base + 2u] = 0x00001000u;
    gte.control[base + 3u] = 0u;
    gte.control[base + 4u] = 0x00001000u;
}

static void prepare_runtime(jojo::PsxRuntime& runtime,
                            std::uint32_t pc = 0x80010000u) {
    jojo::reset_psx_r3000a(runtime.cpu, pc);
    runtime.cpu.cop0.status |= 1u << 30u;
}

static jojo::PsxR3000aStepResult run_command(jojo::PsxRuntime& runtime,
                                              std::uint32_t instruction) {
    constexpr std::uint32_t pc = 0x80010000u;
    prepare_runtime(runtime, pc);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, instruction) ==
          jojo::PsxBusAccessReason::ok);
    return jojo::step_psx_runtime(runtime);
}

static void test_nclip_executes_real_cop2_command() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[12] = pack_sxy(1, 2);
    runtime.gte.data[13] = pack_sxy(5, 3);
    runtime.gte.data[14] = pack_sxy(4, 8);

    const auto step = run_command(runtime, 0x4a140006u);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[24] == 21u);
    CHECK(runtime.cpu.pc == 0x80010004u);
}

static void test_avsz3_updates_mac0_and_otz() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[17] = 0x0100u;
    runtime.gte.data[18] = 0x0200u;
    runtime.gte.data[19] = 0x0300u;
    runtime.gte.control[29] = 0x00001000u;

    const auto step = run_command(runtime, 0x4a15802du);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[24] == 0x00600000u);
    CHECK(runtime.gte.data[7] == 0x0600u);
}

static void test_avsz4_saturates_otz_to_unsigned_16_bit() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[16] = 0xffffu;
    runtime.gte.data[17] = 0xffffu;
    runtime.gte.data[18] = 0xffffu;
    runtime.gte.data[19] = 0xffffu;
    runtime.gte.control[30] = 0x00007fffu;

    const auto step = run_command(runtime, 0x4a16802eu);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[7] == 0xffffu);
}

static void test_mvmva_rotation_v0_translation_with_fraction_shift() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.control[5] = 10u;
    runtime.gte.control[6] = 20u;
    runtime.gte.control[7] = 30u;
    runtime.gte.data[0] = pack_sxy(1, 2);
    runtime.gte.data[1] = 3u;

    const auto step = run_command(runtime, mvmva_instruction(true, 0u, 0u, 0u, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 11u);
    CHECK(runtime.gte.data[26] == 22u);
    CHECK(runtime.gte.data[27] == 33u);
    CHECK(runtime.gte.data[9] == 11u);
    CHECK(runtime.gte.data[10] == 22u);
    CHECK(runtime.gte.data[11] == 33u);
}

static void test_mvmva_selects_light_matrix_v1_and_background_vector() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 8u);
    runtime.gte.control[13] = 100u;
    runtime.gte.control[14] = 200u;
    runtime.gte.control[15] = 300u;
    runtime.gte.data[2] = pack_sxy(4, 5);
    runtime.gte.data[3] = 6u;

    const auto step = run_command(runtime, mvmva_instruction(true, 1u, 1u, 1u, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 104u);
    CHECK(runtime.gte.data[26] == 205u);
    CHECK(runtime.gte.data[27] == 306u);
}

static void test_mvmva_lm_clamps_negative_ir_but_preserves_mac() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.data[0] = pack_sxy(-5, 2);
    runtime.gte.data[1] = 3u;

    const auto step = run_command(runtime, mvmva_instruction(true, 0u, 0u, 3u, true));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 0xfffffffbu);
    CHECK(runtime.gte.data[9] == 0u);
    CHECK(runtime.gte.data[10] == 2u);
    CHECK(runtime.gte.data[11] == 3u);
    CHECK((runtime.gte.control[31] & (1u << 24u)) != 0u);
}

static void test_mvmva_far_color_selector_keeps_hardware_bug() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.control[21] = 1000u;
    runtime.gte.control[22] = 2000u;
    runtime.gte.control[23] = 3000u;
    runtime.gte.data[0] = pack_sxy(1, 2);
    runtime.gte.data[1] = 3u;

    const auto step = run_command(runtime, mvmva_instruction(true, 0u, 0u, 2u, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 0u);
    CHECK(runtime.gte.data[26] == 2u);
    CHECK(runtime.gte.data[27] == 3u);
}

static void test_mvmva_reserved_matrix_uses_documented_garbage_matrix() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[6] = 2u;
    runtime.gte.data[8] = 0x1000u;
    runtime.gte.control[1] = 4u;
    runtime.gte.control[2] = 5u;
    runtime.gte.data[0] = pack_sxy(1, 2);
    runtime.gte.data[1] = 3u;

    const auto step = run_command(runtime, mvmva_instruction(false, 3u, 0u, 3u, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 12320u);
    CHECK(runtime.gte.data[26] == 24u);
    CHECK(runtime.gte.data[27] == 30u);
}

static void test_sqr_squares_ir_and_saturates_positive_results() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[9] = 0xfffffffdu;  // -3
    runtime.gte.data[10] = 4u;
    runtime.gte.data[11] = 0x100u;

    const auto step = run_command(runtime, gte_math_instruction(0x28u, false, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 9u);
    CHECK(runtime.gte.data[26] == 16u);
    CHECK(runtime.gte.data[27] == 0x10000u);
    CHECK(runtime.gte.data[9] == 9u);
    CHECK(runtime.gte.data[10] == 16u);
    CHECK(runtime.gte.data[11] == 0x7fffu);
    CHECK((runtime.gte.control[31] & (1u << 22u)) != 0u);
}

static void test_op_uses_rotation_diagonal_as_second_vector() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[9] = 1u;
    runtime.gte.data[10] = 2u;
    runtime.gte.data[11] = 3u;
    runtime.gte.control[0] = 4u; // RT11
    runtime.gte.control[2] = 5u; // RT22
    runtime.gte.control[4] = 6u; // RT33

    const auto step = run_command(runtime, gte_math_instruction(0x0cu, false, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 3u);
    CHECK(runtime.gte.data[26] == 0xfffffffau);
    CHECK(runtime.gte.data[27] == 3u);
    CHECK(runtime.gte.data[9] == 3u);
    CHECK(runtime.gte.data[10] == 0xfffffffau);
    CHECK(runtime.gte.data[11] == 3u);
}

static void test_gpf_updates_ir_and_pushes_saturated_color_fifo() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[6] = 0x2c000000u;
    runtime.gte.data[8] = 0x1000u;
    runtime.gte.data[9] = 0x1000u;
    runtime.gte.data[10] = 0x0800u;
    runtime.gte.data[11] = 0x0400u;
    runtime.gte.data[20] = 0x11111111u;
    runtime.gte.data[21] = 0x22222222u;
    runtime.gte.data[22] = 0x33333333u;

    const auto step = run_command(runtime, gte_math_instruction(0x3du, true, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 0x1000u);
    CHECK(runtime.gte.data[26] == 0x0800u);
    CHECK(runtime.gte.data[27] == 0x0400u);
    CHECK(runtime.gte.data[20] == 0x22222222u);
    CHECK(runtime.gte.data[21] == 0x33333333u);
    CHECK(runtime.gte.data[22] == 0x2c4080ffu);
    CHECK((runtime.gte.control[31] & (1u << 21u)) != 0u);
}

static void test_gpl_accumulates_existing_mac_before_fraction_shift() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[8] = 0x1000u;
    runtime.gte.data[9] = 1u;
    runtime.gte.data[10] = 2u;
    runtime.gte.data[11] = 3u;
    runtime.gte.data[25] = 10u;
    runtime.gte.data[26] = 20u;
    runtime.gte.data[27] = 30u;
    runtime.gte.data[6] = 0x34000000u;

    const auto step = run_command(runtime, gte_math_instruction(0x3eu, true, false));
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 11u);
    CHECK(runtime.gte.data[26] == 22u);
    CHECK(runtime.gte.data[27] == 33u);
    CHECK(runtime.gte.data[9] == 11u);
    CHECK(runtime.gte.data[10] == 22u);
    CHECK(runtime.gte.data[11] == 33u);
    CHECK((runtime.gte.data[22] >> 24u) == 0x34u);
}

static void test_lwc2_loads_ram_word_into_gte_data_register() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, base + 4u, 0x12345678u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xc8220004u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[2] == 0x12345678u);
    CHECK(runtime.cpu.pc == pc + 4u);
}

static void test_swc2_stores_gte_data_register_into_ram() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    jojo::psx_gte_write_data(runtime.gte, 2u, 0x89abcdefu);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xe8220008u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    const auto stored = jojo::psx_bus_read_u32(runtime.bus, base + 8u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0x89abcdefu);
}

static void test_lwc2_requires_enabled_cop2() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    jojo::reset_psx_r3000a(runtime.cpu, pc);
    runtime.cpu.gpr[1] = 0x80010100u;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xc8220000u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(step.exception_code == jojo::PsxR3000aExceptionCode::coprocessor_unusable);
    CHECK(((runtime.cpu.cop0.cause >> 28u) & 3u) == 2u);
}

static void test_lwc2_misalignment_raises_address_error_load() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xc8220002u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(step.exception_code == jojo::PsxR3000aExceptionCode::address_error_load);
    CHECK(runtime.cpu.cop0.bad_vaddr == base + 2u);
}

static void test_swc2_misalignment_raises_address_error_store() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xe8220002u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(step.exception_code == jojo::PsxR3000aExceptionCode::address_error_store);
    CHECK(runtime.cpu.cop0.bad_vaddr == base + 2u);
}

int main() {
    test_nclip_executes_real_cop2_command();
    test_avsz3_updates_mac0_and_otz();
    test_avsz4_saturates_otz_to_unsigned_16_bit();
    test_mvmva_rotation_v0_translation_with_fraction_shift();
    test_mvmva_selects_light_matrix_v1_and_background_vector();
    test_mvmva_lm_clamps_negative_ir_but_preserves_mac();
    test_mvmva_far_color_selector_keeps_hardware_bug();
    test_mvmva_reserved_matrix_uses_documented_garbage_matrix();
    test_sqr_squares_ir_and_saturates_positive_results();
    test_op_uses_rotation_diagonal_as_second_vector();
    test_gpf_updates_ir_and_pushes_saturated_color_fifo();
    test_gpl_accumulates_existing_mac_before_fraction_shift();
    test_lwc2_loads_ram_word_into_gte_data_register();
    test_swc2_stores_gte_data_register_into_ram();
    test_lwc2_requires_enabled_cop2();
    test_lwc2_misalignment_raises_address_error_load();
    test_swc2_misalignment_raises_address_error_store();
    if (failures) return 1;
    std::cout << "PS1 GTE command assertions passed\n";
    return 0;
}
