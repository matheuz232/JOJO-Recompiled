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

    auto i = jojo::decode_sh4(0x0127, 0); // MUL.L R2,R1
    CHECK(i.op == Sh4Op::mul_l);
    CHECK(i.rn == 1 && i.rm == 2);

    i = jojo::decode_sh4(0x212F, 0); // MULS.W R2,R1
    CHECK(i.op == Sh4Op::muls_w);
    CHECK(i.rn == 1 && i.rm == 2);

    i = jojo::decode_sh4(0x212E, 0); // MULU.W R2,R1
    CHECK(i.op == Sh4Op::mulu_w);
    CHECK(i.rn == 1 && i.rm == 2);

    i = jojo::decode_sh4(0x312D, 0); // DMULS.L R2,R1
    CHECK(i.op == Sh4Op::dmuls_l);
    CHECK(i.rn == 1 && i.rm == 2);

    i = jojo::decode_sh4(0x3125, 0); // DMULU.L R2,R1
    CHECK(i.op == Sh4Op::dmulu_l);
    CHECK(i.rn == 1 && i.rm == 2);

    CHECK(jojo::decode_sh4(0x0028, 0).op == Sh4Op::clrmac);
}

static void test_clrmac_zeroes_both_halves_without_touching_t() {
    jojo::Sh4ReferenceState state{};
    state.mach = 0x11223344u;
    state.macl = 0x55667788u;
    state.t = true;
    state.pr = 0xDEAD0000u;

    const bool ok = execute_words({0x0028, 0x000B, 0x0009}, state);
    if (!ok) return;

    CHECK(state.mach == 0u);
    CHECK(state.macl == 0u);
    CHECK(state.t);
}

static void test_mul_l_writes_low_32_bits_and_preserves_mach() {
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0xFFFFFFFFu;
    state.r[2] = 2u;
    state.mach = 0xAABBCCDDu;
    state.pr = 0xDEAD1000u;

    const bool ok = execute_words({0x0127, 0x000B, 0x0009}, state);
    if (!ok) return;

    CHECK(state.macl == 0xFFFFFFFEu);
    CHECK(state.mach == 0xAABBCCDDu);
}

static void test_word_multiply_signed_and_unsigned_are_distinct() {
    jojo::Sh4ReferenceState signed_state{};
    signed_state.r[1] = 0x0000FFFEu;
    signed_state.r[2] = 3u;
    signed_state.mach = 0x12345678u;
    signed_state.pr = 0xDEAD2000u;

    const bool signed_ok = execute_words({0x212F, 0x000B, 0x0009}, signed_state);
    if (!signed_ok) return;
    CHECK(signed_state.macl == 0xFFFFFFFAu);
    CHECK(signed_state.mach == 0x12345678u);

    jojo::Sh4ReferenceState unsigned_state{};
    unsigned_state.r[1] = 0x0000FFFEu;
    unsigned_state.r[2] = 3u;
    unsigned_state.mach = 0x87654321u;
    unsigned_state.pr = 0xDEAD3000u;

    const bool unsigned_ok = execute_words({0x212E, 0x000B, 0x0009}, unsigned_state);
    if (!unsigned_ok) return;
    CHECK(unsigned_state.macl == 0x0002FFFAu);
    CHECK(unsigned_state.mach == 0x87654321u);
}

static void test_double_multiply_signed_and_unsigned_write_full_mac() {
    jojo::Sh4ReferenceState signed_state{};
    signed_state.r[1] = 0xFFFFFFFEu;
    signed_state.r[2] = 3u;
    signed_state.pr = 0xDEAD4000u;

    const bool signed_ok = execute_words({0x312D, 0x000B, 0x0009}, signed_state);
    if (!signed_ok) return;
    CHECK(signed_state.mach == 0xFFFFFFFFu);
    CHECK(signed_state.macl == 0xFFFFFFFAu);

    jojo::Sh4ReferenceState unsigned_state{};
    unsigned_state.r[1] = 0xFFFFFFFFu;
    unsigned_state.r[2] = 2u;
    unsigned_state.pr = 0xDEAD5000u;

    const bool unsigned_ok = execute_words({0x3125, 0x000B, 0x0009}, unsigned_state);
    if (!unsigned_ok) return;
    CHECK(unsigned_state.mach == 0x00000001u);
    CHECK(unsigned_state.macl == 0xFFFFFFFEu);
}

int main() {
    test_decoder_patterns();
    test_clrmac_zeroes_both_halves_without_touching_t();
    test_mul_l_writes_low_32_bits_and_preserves_mach();
    test_word_multiply_signed_and_unsigned_are_distinct();
    test_double_multiply_signed_and_unsigned_write_full_mac();
    if (failures) {
        std::cerr << failures << " SH-4 multiply assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 multiply assertions passed\n";
    return 0;
}
