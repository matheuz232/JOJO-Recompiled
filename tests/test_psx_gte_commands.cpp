#include "core/psx_runtime.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t pack_sxy(std::int16_t x, std::int16_t y) {
    return static_cast<std::uint16_t>(x) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16u);
}

static std::uint32_t pack_rgb(std::uint8_t r, std::uint8_t g,
                              std::uint8_t b, std::uint8_t code = 0x2cu) {
    return static_cast<std::uint32_t>(r) |
           (static_cast<std::uint32_t>(g) << 8u) |
           (static_cast<std::uint32_t>(b) << 16u) |
           (static_cast<std::uint32_t>(code) << 24u);
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
    runtime.gte.data[9] = 0xfffffffdu;
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
    runtime.gte.control[0] = 4u;
    runtime.gte.control[2] = 5u;
    runtime.gte.control[4] = 6u;

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

static void test_rtps_projects_v0_and_advances_screen_depth_fifos() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.data[0] = pack_sxy(100, 200);
    runtime.gte.data[1] = 400u;
    runtime.gte.data[12] = pack_sxy(1, 2);
    runtime.gte.data[13] = pack_sxy(3, 4);
    runtime.gte.data[14] = pack_sxy(5, 6);
    runtime.gte.data[16] = 10u;
    runtime.gte.data[17] = 20u;
    runtime.gte.data[18] = 30u;
    runtime.gte.data[19] = 40u;
    runtime.gte.control[24] = 0u;
    runtime.gte.control[25] = 0u;
    runtime.gte.control[26] = 200u;
    runtime.gte.control[27] = 0u;
    runtime.gte.control[28] = 0u;

    const auto step = run_command(runtime, 0x4a180001u);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[25] == 100u);
    CHECK(runtime.gte.data[26] == 200u);
    CHECK(runtime.gte.data[27] == 400u);
    CHECK(runtime.gte.data[9] == 100u);
    CHECK(runtime.gte.data[10] == 200u);
    CHECK(runtime.gte.data[11] == 400u);
    CHECK(runtime.gte.data[16] == 20u);
    CHECK(runtime.gte.data[17] == 30u);
    CHECK(runtime.gte.data[18] == 40u);
    CHECK(runtime.gte.data[19] == 400u);
    CHECK(runtime.gte.data[12] == pack_sxy(3, 4));
    CHECK(runtime.gte.data[13] == pack_sxy(5, 6));
    CHECK(runtime.gte.data[14] == pack_sxy(50, 100));
    CHECK(runtime.gte.data[8] == 0u);
}

static void test_rtps_applies_screen_offsets_and_depth_cue_factor() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.data[0] = pack_sxy(100, 200);
    runtime.gte.data[1] = 400u;
    runtime.gte.control[24] = 10u * 0x10000u;
    runtime.gte.control[25] = static_cast<std::uint32_t>(-20 * 0x10000);
    runtime.gte.control[26] = 200u;
    runtime.gte.control[27] = 0x0100u;
    runtime.gte.control[28] = 0u;

    const auto step = run_command(runtime, 0x4a180001u);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[14] == pack_sxy(60, 80));
    CHECK(runtime.gte.data[8] == 0x0800u);
    CHECK(runtime.gte.data[24] == 0x00800000u);
}

static void test_rtps_near_clip_saturates_division_and_screen_x() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.data[0] = pack_sxy(1000, 0);
    runtime.gte.data[1] = 100u;
    runtime.gte.control[26] = 200u;

    const auto step = run_command(runtime, 0x4a180001u);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[19] == 100u);
    CHECK(static_cast<std::int16_t>(runtime.gte.data[14] & 0xffffu) == 0x03ff);
    CHECK((runtime.gte.control[31] & (1u << 17u)) != 0u);
    CHECK((runtime.gte.control[31] & (1u << 14u)) != 0u);
    CHECK((runtime.gte.control[31] & 0x80000000u) != 0u);
}

static void test_rtpt_projects_three_vertices_and_leaves_final_vector_in_ir() {
    jojo::PsxRuntime runtime{};
    set_identity_matrix(runtime.gte, 0u);
    runtime.gte.data[0] = pack_sxy(100, 0);
    runtime.gte.data[1] = 400u;
    runtime.gte.data[2] = pack_sxy(0, 100);
    runtime.gte.data[3] = 400u;
    runtime.gte.data[4] = pack_sxy(-100, -100);
    runtime.gte.data[5] = 400u;
    runtime.gte.data[16] = 10u;
    runtime.gte.data[17] = 20u;
    runtime.gte.data[18] = 30u;
    runtime.gte.data[19] = 40u;
    runtime.gte.control[26] = 200u;

    const auto step = run_command(runtime, 0x4a280030u);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[12] == pack_sxy(50, 0));
    CHECK(runtime.gte.data[13] == pack_sxy(0, 50));
    CHECK(runtime.gte.data[14] == pack_sxy(-50, -50));
    CHECK(runtime.gte.data[16] == 40u);
    CHECK(runtime.gte.data[17] == 400u);
    CHECK(runtime.gte.data[18] == 400u);
    CHECK(runtime.gte.data[19] == 400u);
    CHECK(runtime.gte.data[9] == 0xffffff9cu);
    CHECK(runtime.gte.data[10] == 0xffffff9cu);
    CHECK(runtime.gte.data[11] == 400u);
}

static void seed_identity_lighting(jojo::PsxGteState& gte) {
    set_identity_matrix(gte, 8u);
    set_identity_matrix(gte, 16u);
    gte.control[13] = 0u;
    gte.control[14] = 0u;
    gte.control[15] = 0u;
    gte.data[6] = pack_rgb(16, 32, 48);
}

static void seed_far_color(jojo::PsxGteState& gte) {
    gte.control[21] = 32u * 16u;
    gte.control[22] = 64u * 16u;
    gte.control[23] = 96u * 16u;
    gte.data[8] = 0x0800u;
}

static void test_documented_gte_color_command_set() {
    {
        jojo::PsxRuntime runtime{};
        runtime.gte.data[6] = pack_rgb(16, 32, 48);
        seed_far_color(runtime.gte);
        const auto step = run_command(runtime, gte_math_instruction(0x10u, true, false));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(24, 48, 72));
    }
    {
        jojo::PsxRuntime runtime{};
        runtime.gte.data[9] = 0x0100u;
        runtime.gte.data[10] = 0x0200u;
        runtime.gte.data[11] = 0x0300u;
        seed_far_color(runtime.gte);
        runtime.gte.data[6] = pack_rgb(0, 0, 0);
        const auto step = run_command(runtime, gte_math_instruction(0x11u, true, false));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[25] == 0x0180u);
        CHECK(runtime.gte.data[26] == 0x0300u);
        CHECK(runtime.gte.data[27] == 0x0480u);
    }
    {
        jojo::PsxRuntime runtime{};
        runtime.gte.data[6] = pack_rgb(16, 32, 48);
        runtime.gte.data[9] = 0x1000u;
        runtime.gte.data[10] = 0x1000u;
        runtime.gte.data[11] = 0x1000u;
        seed_far_color(runtime.gte);
        const auto step = run_command(runtime, gte_math_instruction(0x29u, true, false));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(24, 48, 72));
    }
    {
        jojo::PsxRuntime runtime{};
        runtime.gte.data[6] = pack_rgb(0, 0, 0, 0x34u);
        runtime.gte.data[8] = 0u;
        runtime.gte.data[20] = pack_rgb(10, 20, 30, 1u);
        runtime.gte.data[21] = pack_rgb(40, 50, 60, 2u);
        runtime.gte.data[22] = pack_rgb(70, 80, 90, 3u);
        const auto step = run_command(runtime, gte_math_instruction(0x2au, true, false));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[20] == pack_rgb(10, 20, 30, 0x34u));
        CHECK(runtime.gte.data[21] == pack_rgb(40, 50, 60, 0x34u));
        CHECK(runtime.gte.data[22] == pack_rgb(70, 80, 90, 0x34u));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[0] = pack_sxy(0x0100, 0x0200);
        runtime.gte.data[1] = 0x0300u;
        const auto step = run_command(runtime, gte_math_instruction(0x1eu, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(16, 32, 48));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[0] = pack_sxy(0x0100, 0);
        runtime.gte.data[1] = 0u;
        runtime.gte.data[2] = pack_sxy(0, 0x0200);
        runtime.gte.data[3] = 0u;
        runtime.gte.data[4] = pack_sxy(0, 0);
        runtime.gte.data[5] = 0x0300u;
        const auto step = run_command(runtime, gte_math_instruction(0x20u, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[20] == pack_rgb(16, 0, 0));
        CHECK(runtime.gte.data[21] == pack_rgb(0, 32, 0));
        CHECK(runtime.gte.data[22] == pack_rgb(0, 0, 48));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[0] = pack_sxy(0x1000, 0x1000);
        runtime.gte.data[1] = 0x1000u;
        const auto step = run_command(runtime, gte_math_instruction(0x1bu, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(16, 32, 48));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[0] = pack_sxy(0x1000, 0x1000);
        runtime.gte.data[1] = 0x1000u;
        runtime.gte.data[2] = runtime.gte.data[0];
        runtime.gte.data[3] = runtime.gte.data[1];
        runtime.gte.data[4] = runtime.gte.data[0];
        runtime.gte.data[5] = runtime.gte.data[1];
        const auto step = run_command(runtime, gte_math_instruction(0x3fu, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[20] == pack_rgb(16, 32, 48));
        CHECK(runtime.gte.data[21] == pack_rgb(16, 32, 48));
        CHECK(runtime.gte.data[22] == pack_rgb(16, 32, 48));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[0] = pack_sxy(0x1000, 0x1000);
        runtime.gte.data[1] = 0x1000u;
        seed_far_color(runtime.gte);
        const auto step = run_command(runtime, gte_math_instruction(0x13u, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(24, 48, 72));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[0] = pack_sxy(0x1000, 0x1000);
        runtime.gte.data[1] = 0x1000u;
        runtime.gte.data[2] = runtime.gte.data[0];
        runtime.gte.data[3] = runtime.gte.data[1];
        runtime.gte.data[4] = runtime.gte.data[0];
        runtime.gte.data[5] = runtime.gte.data[1];
        seed_far_color(runtime.gte);
        const auto step = run_command(runtime, gte_math_instruction(0x16u, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[20] == pack_rgb(24, 48, 72));
        CHECK(runtime.gte.data[21] == pack_rgb(24, 48, 72));
        CHECK(runtime.gte.data[22] == pack_rgb(24, 48, 72));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[9] = 0x1000u;
        runtime.gte.data[10] = 0x1000u;
        runtime.gte.data[11] = 0x1000u;
        const auto step = run_command(runtime, gte_math_instruction(0x1cu, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(16, 32, 48));
    }
    {
        jojo::PsxRuntime runtime{};
        seed_identity_lighting(runtime.gte);
        runtime.gte.data[9] = 0x1000u;
        runtime.gte.data[10] = 0x1000u;
        runtime.gte.data[11] = 0x1000u;
        seed_far_color(runtime.gte);
        const auto step = run_command(runtime, gte_math_instruction(0x14u, true, true));
        CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.gte.data[22] == pack_rgb(24, 48, 72));
    }
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
    test_rtps_projects_v0_and_advances_screen_depth_fifos();
    test_rtps_applies_screen_offsets_and_depth_cue_factor();
    test_rtps_near_clip_saturates_division_and_screen_x();
    test_rtpt_projects_three_vertices_and_leaves_final_vector_in_ir();
    test_documented_gte_color_command_set();
    test_lwc2_loads_ram_word_into_gte_data_register();
    test_swc2_stores_gte_data_register_into_ram();
    test_lwc2_requires_enabled_cop2();
    test_lwc2_misalignment_raises_address_error_load();
    test_swc2_misalignment_raises_address_error_store();
    if (failures) return 1;
    std::cout << "PS1 GTE command assertions passed\n";
    return 0;
}
