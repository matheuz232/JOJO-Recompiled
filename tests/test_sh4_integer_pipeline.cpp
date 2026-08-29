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
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 32);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_patterns() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0x0008, 0).op == Sh4Op::clrt);
    CHECK(jojo::decode_sh4(0x0018, 0).op == Sh4Op::sett);
    auto i = jojo::decode_sh4(0x0329, 0);
    CHECK(i.op == Sh4Op::movt && i.rn == 3);

    i = jojo::decode_sh4(0x88FB, 0);
    CHECK(i.op == Sh4Op::cmp_eq_imm && i.immediate == -5);
    CHECK(jojo::decode_sh4(0x3122, 0).op == Sh4Op::cmp_hs_reg);
    CHECK(jojo::decode_sh4(0x3123, 0).op == Sh4Op::cmp_ge_reg);
    CHECK(jojo::decode_sh4(0x3126, 0).op == Sh4Op::cmp_hi_reg);
    CHECK(jojo::decode_sh4(0x3127, 0).op == Sh4Op::cmp_gt_reg);
    CHECK(jojo::decode_sh4(0x4111, 0).op == Sh4Op::cmp_pz);
    CHECK(jojo::decode_sh4(0x4215, 0).op == Sh4Op::cmp_pl);
    i = jojo::decode_sh4(0x4510, 0);
    CHECK(i.op == Sh4Op::dt && i.rn == 5); // DT R5

    CHECK(jojo::decode_sh4(0x2458, 0).op == Sh4Op::tst_reg);
    CHECK(jojo::decode_sh4(0x2459, 0).op == Sh4Op::and_reg);
    CHECK(jojo::decode_sh4(0x245A, 0).op == Sh4Op::xor_reg);
    CHECK(jojo::decode_sh4(0x245B, 0).op == Sh4Op::or_reg);
    CHECK(jojo::decode_sh4(0xC8A5, 0).op == Sh4Op::tst_imm);
    CHECK(jojo::decode_sh4(0xC9A5, 0).op == Sh4Op::and_imm);
    CHECK(jojo::decode_sh4(0xCAA5, 0).op == Sh4Op::xor_imm);
    CHECK(jojo::decode_sh4(0xCBA5, 0).op == Sh4Op::or_imm);

    CHECK(jojo::decode_sh4(0x6857, 0).op == Sh4Op::not_reg);
    CHECK(jojo::decode_sh4(0x695B, 0).op == Sh4Op::neg_reg);
    CHECK(jojo::decode_sh4(0x4A00, 0).op == Sh4Op::shll);
    CHECK(jojo::decode_sh4(0x4B01, 0).op == Sh4Op::shlr);
    CHECK(jojo::decode_sh4(0x4C21, 0).op == Sh4Op::shar);
    CHECK(jojo::decode_sh4(0x4D08, 0).op == Sh4Op::shll2);
    CHECK(jojo::decode_sh4(0x4D09, 0).op == Sh4Op::shlr2);
    CHECK(jojo::decode_sh4(0x4D18, 0).op == Sh4Op::shll8);
    CHECK(jojo::decode_sh4(0x4D19, 0).op == Sh4Op::shlr8);
    CHECK(jojo::decode_sh4(0x4D28, 0).op == Sh4Op::shll16);
    CHECK(jojo::decode_sh4(0x4D29, 0).op == Sh4Op::shlr16);
}

static void test_logic_t_and_unary_execution() {
    jojo::Sh4ReferenceState state{};
    state.pr = 0xDEAD0000u;
    const bool ok = execute_words({
        0xE005, // MOV #5,R0
        0x8805, // CMP/EQ #5,R0
        0x0129, // MOVT R1
        0x0008, // CLRT
        0x0229, // MOVT R2
        0x0018, // SETT
        0x0329, // MOVT R3
        0xE40F, // MOV #15,R4
        0xE503, // MOV #3,R5
        0x2459, // AND R5,R4
        0x245B, // OR R5,R4
        0x245A, // XOR R5,R4
        0x2458, // TST R5,R4
        0x0629, // MOVT R6
        0xC90F, // AND #0x0F,R0
        0xCA01, // XOR #1,R0
        0xCB80, // OR #0x80,R0
        0xC880, // TST #0x80,R0
        0x0729, // MOVT R7
        0x6857, // NOT R5,R8
        0x695B, // NEG R5,R9
        0x000B, // RTS
        0x0009, // NOP delay slot
    }, state);
    if (!ok) return;

    CHECK(state.r[0] == 0x84u);
    CHECK(state.r[1] == 1u);
    CHECK(state.r[2] == 0u);
    CHECK(state.r[3] == 1u);
    CHECK(state.r[4] == 0u);
    CHECK(state.r[6] == 1u);
    CHECK(state.r[7] == 0u);
    CHECK(state.r[8] == 0xFFFFFFFCu);
    CHECK(state.r[9] == 0xFFFFFFFDu);
    CHECK(!state.t);
    CHECK(state.pc == 0xDEAD0000u);
}

static void test_signed_and_unsigned_comparisons() {
    jojo::Sh4ReferenceState state{};
    state.pr = 0xDEAD1000u;
    const bool ok = execute_words({
        0xE1FF, // MOV #-1,R1
        0xE201, // MOV #1,R2
        0x3122, // CMP/HS R2,R1 (unsigned >=)
        0x0329, // MOVT R3
        0x3123, // CMP/GE R2,R1 (signed >=)
        0x0429, // MOVT R4
        0x3126, // CMP/HI R2,R1 (unsigned >)
        0x0529, // MOVT R5
        0x3127, // CMP/GT R2,R1 (signed >)
        0x0629, // MOVT R6
        0x4111, // CMP/PZ R1
        0x0729, // MOVT R7
        0x4215, // CMP/PL R2
        0x0829, // MOVT R8
        0x000B,
        0x0009,
    }, state);
    if (!ok) return;

    CHECK(state.r[3] == 1u);
    CHECK(state.r[4] == 0u);
    CHECK(state.r[5] == 1u);
    CHECK(state.r[6] == 0u);
    CHECK(state.r[7] == 0u);
    CHECK(state.r[8] == 1u);
    CHECK(state.t);
}

static void test_dt_decrements_and_sets_t_only_on_zero() {
    jojo::Sh4ReferenceState state{};
    state.r[5] = 2u;
    state.r[6] = 0u;
    state.t = true;
    state.pr = 0xDEAD1800u;
    const bool ok = execute_words({
        0x4510, // DT R5 -> 1, T=0
        0x4710, // DT R7: R7 starts 0 -> 0xFFFFFFFF, T=0
        0x4510, // DT R5 -> 0, T=1
        0x0629, // MOVT R6 -> 1
        0x000B,
        0x0009,
    }, state, 0x8C011800u);
    if (!ok) return;

    CHECK(state.r[5] == 0u);
    CHECK(state.r[7] == 0xFFFFFFFFu);
    CHECK(state.r[6] == 1u);
    CHECK(state.t);
}

static void test_shift_semantics_and_t_bit() {
    jojo::Sh4ReferenceState state{};
    state.r[10] = 0x80000001u;
    state.r[11] = 0x80000001u;
    state.r[12] = 0x80000000u;
    state.r[13] = 1u;
    state.pr = 0xDEAD2000u;
    const bool ok = execute_words({
        0x4A00, // SHLL R10: T <- old bit31
        0x0029, // MOVT R0
        0x4B01, // SHLR R11: T <- old bit0
        0x0129, // MOVT R1
        0x4C21, // SHAR R12: T <- old bit0
        0x0229, // MOVT R2
        0x0018, // SETT
        0x4D08, // SHLL2 R13
        0x4D18, // SHLL8 R13
        0x4D09, // SHLR2 R13
        0x4D19, // SHLR8 R13
        0x4D28, // SHLL16 R13
        0x4D29, // SHLR16 R13
        0x0E29, // MOVT R14; constant shifts must not change T
        0x000B,
        0x0009,
    }, state);
    if (!ok) return;

    CHECK(state.r[10] == 2u);
    CHECK(state.r[0] == 1u);
    CHECK(state.r[11] == 0x40000000u);
    CHECK(state.r[1] == 1u);
    CHECK(state.r[12] == 0xC0000000u);
    CHECK(state.r[2] == 0u);
    CHECK(state.r[13] == 1u);
    CHECK(state.r[14] == 1u);
    CHECK(state.t);
}

int main() {
    test_decoder_patterns();
    test_logic_t_and_unary_execution();
    test_signed_and_unsigned_comparisons();
    test_dt_decrements_and_sets_t_only_on_zero();
    test_shift_semantics_and_t_bit();
    if (failures) {
        std::cerr << failures << " expanded SH-4 integer assertion(s) failed\n";
        return 1;
    }
    std::cout << "all expanded SH-4 integer assertions passed\n";
    return 0;
}
