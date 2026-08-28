#include "core/sh4_cfg.h"
#include "core/sh4_decoder.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static bool execute_words(const std::vector<std::uint16_t>& words,
                          jojo::Sh4ReferenceState& state,
                          jojo::Sh4ReferenceMemoryView memory,
                          std::uint32_t base = 0x8C010000u) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2u);
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    CHECK(cfg);
    if (!cfg) return false;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return false;
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, memory, 32);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_patterns_and_scaled_displacements() {
    using jojo::Sh4Op;
    auto i = jojo::decode_sh4(0x8033, 0);
    CHECK(i.op == Sh4Op::movb_store_disp && i.rn == 3 && i.rm == 0 && i.displacement == 3);
    i = jojo::decode_sh4(0x8142, 0);
    CHECK(i.op == Sh4Op::movw_store_disp && i.rn == 4 && i.rm == 0 && i.displacement == 4);
    i = jojo::decode_sh4(0x1563, 0);
    CHECK(i.op == Sh4Op::movl_store_disp && i.rn == 5 && i.rm == 6 && i.displacement == 12);
    i = jojo::decode_sh4(0x8433, 0);
    CHECK(i.op == Sh4Op::movb_load_disp && i.rn == 0 && i.rm == 3 && i.displacement == 3);
    i = jojo::decode_sh4(0x8542, 0);
    CHECK(i.op == Sh4Op::movw_load_disp && i.rn == 0 && i.rm == 4 && i.displacement == 4);
    i = jojo::decode_sh4(0x5753, 0);
    CHECK(i.op == Sh4Op::movl_load_disp && i.rn == 7 && i.rm == 5 && i.displacement == 12);

    CHECK(jojo::decode_sh4(0x0124, 0).op == Sh4Op::movb_store_index);
    CHECK(jojo::decode_sh4(0x0325, 0).op == Sh4Op::movw_store_index);
    CHECK(jojo::decode_sh4(0x0426, 0).op == Sh4Op::movl_store_index);
    CHECK(jojo::decode_sh4(0x051C, 0).op == Sh4Op::movb_load_index);
    CHECK(jojo::decode_sh4(0x063D, 0).op == Sh4Op::movw_load_index);
    CHECK(jojo::decode_sh4(0x074E, 0).op == Sh4Op::movl_load_index);
}

static void test_displacement_execution() {
    std::vector<std::uint8_t> memory(64, 0);
    jojo::Sh4ReferenceState state{};
    state.r[0] = 0xAABBCC80u;
    state.r[3] = 0x9000u;
    state.r[4] = 0x9008u;
    state.r[5] = 0x9010u;
    state.r[6] = 0x12345678u;
    state.pr = 0xDEAD0000u;

    const bool ok = execute_words({
        0x8033, // MOV.B R0,@(3,R3) -> 0x9003
        0x8142, // MOV.W R0,@(2,R4) -> 0x900C (disp*2)
        0x1563, // MOV.L R6,@(3,R5) -> 0x901C (disp*4)
        0x8433, // MOV.B @(3,R3),R0 -> sign extend
        0x8542, // MOV.W @(2,R4),R0 -> sign extend
        0x5753, // MOV.L @(3,R5),R7
        0x000B,
        0x0009,
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(memory[3] == 0x80u);
    CHECK(memory[12] == 0x80u && memory[13] == 0xCCu);
    CHECK(memory[28] == 0x78u && memory[29] == 0x56u && memory[30] == 0x34u && memory[31] == 0x12u);
    CHECK(state.r[0] == 0xFFFFCC80u);
    CHECK(state.r[7] == 0x12345678u);
}

static void test_r0_indexed_execution() {
    std::vector<std::uint8_t> memory(48, 0);
    jojo::Sh4ReferenceState state{};
    state.r[0] = 4u;
    state.r[1] = 0xA000u;
    state.r[2] = 0xAABBCC80u;
    state.r[3] = 0xA008u;
    state.r[4] = 0xA010u;
    state.pr = 0xDEAD1000u;

    const bool ok = execute_words({
        0x0124, // MOV.B R2,@(R0,R1) -> 0xA004
        0x0325, // MOV.W R2,@(R0,R3) -> 0xA00C
        0x0426, // MOV.L R2,@(R0,R4) -> 0xA014
        0x051C, // MOV.B @(R0,R1),R5
        0x063D, // MOV.W @(R0,R3),R6
        0x074E, // MOV.L @(R0,R4),R7
        0x000B,
        0x0009,
    }, state, {0xA000u, memory});
    if (!ok) return;

    CHECK(state.r[0] == 4u);
    CHECK(state.r[5] == 0xFFFFFF80u);
    CHECK(state.r[6] == 0xFFFFCC80u);
    CHECK(state.r[7] == 0xAABBCC80u);
}

static void test_index_address_wraps_in_32_bits() {
    std::vector<std::uint8_t> memory(16, 0);
    jojo::Sh4ReferenceState state{};
    state.r[0] = 8u;
    state.r[1] = 0xFFFFFFFCu;
    state.r[2] = 0x11223344u;
    state.pr = 0xDEAD2000u;
    const bool ok = execute_words({
        0x0126, // MOV.L R2,@(R0,R1): 0xFFFFFFFC + 8 -> 0x00000004
        0x000B,
        0x0009,
    }, state, {0u, memory});
    if (!ok) return;
    CHECK(memory[4] == 0x44u && memory[5] == 0x33u && memory[6] == 0x22u && memory[7] == 0x11u);
}

int main() {
    test_decoder_patterns_and_scaled_displacements();
    test_displacement_execution();
    test_r0_indexed_execution();
    test_index_address_wraps_in_32_bits();
    if (failures) {
        std::cerr << failures << " SH-4 addressing assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 addressing assertions passed\n";
    return 0;
}
