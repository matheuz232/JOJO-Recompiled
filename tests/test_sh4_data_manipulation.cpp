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

    auto i = jojo::decode_sh4(0x612E, 0); // EXTS.B R2,R1
    CHECK(i.op == Sh4Op::exts_b && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x612F, 0); // EXTS.W R2,R1
    CHECK(i.op == Sh4Op::exts_w && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x612C, 0); // EXTU.B R2,R1
    CHECK(i.op == Sh4Op::extu_b && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x612D, 0); // EXTU.W R2,R1
    CHECK(i.op == Sh4Op::extu_w && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x6128, 0); // SWAP.B R2,R1
    CHECK(i.op == Sh4Op::swap_b && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x6129, 0); // SWAP.W R2,R1
    CHECK(i.op == Sh4Op::swap_w && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x212D, 0); // XTRCT R2,R1
    CHECK(i.op == Sh4Op::xtrct && i.rn == 1 && i.rm == 2);
}

static void test_extensions_preserve_t_and_use_low_byte_or_word() {
    jojo::Sh4ReferenceState state{};
    state.r[2] = 0x12348080u;
    state.t = true;
    state.pr = 0xDEAD0000u;

    bool ok = execute_words({0x612E, 0x000B, 0x0009}, state);
    if (!ok) return;
    CHECK(state.r[1] == 0xFFFFFF80u);
    CHECK(state.t);

    state.r[2] = 0x12348001u;
    state.pr = 0xDEAD0010u;
    ok = execute_words({0x612F, 0x000B, 0x0009}, state);
    if (!ok) return;
    CHECK(state.r[1] == 0xFFFF8001u);
    CHECK(state.t);

    state.r[2] = 0xABCDEF80u;
    state.pr = 0xDEAD0020u;
    ok = execute_words({0x612C, 0x000B, 0x0009}, state);
    if (!ok) return;
    CHECK(state.r[1] == 0x00000080u);
    CHECK(state.t);

    state.r[2] = 0xABCD8001u;
    state.pr = 0xDEAD0030u;
    ok = execute_words({0x612D, 0x000B, 0x0009}, state);
    if (!ok) return;
    CHECK(state.r[1] == 0x00008001u);
    CHECK(state.t);
}

static void test_swap_byte_and_word() {
    jojo::Sh4ReferenceState byte_state{};
    byte_state.r[2] = 0xA1B2C3D4u;
    byte_state.t = true;
    byte_state.pr = 0xDEAD1000u;
    const bool byte_ok = execute_words({0x6128, 0x000B, 0x0009}, byte_state);
    if (!byte_ok) return;
    CHECK(byte_state.r[1] == 0xA1B2D4C3u);
    CHECK(byte_state.r[2] == 0xA1B2C3D4u);
    CHECK(byte_state.t);

    jojo::Sh4ReferenceState word_state{};
    word_state.r[2] = 0xA1B2C3D4u;
    word_state.t = true;
    word_state.pr = 0xDEAD1010u;
    const bool word_ok = execute_words({0x6129, 0x000B, 0x0009}, word_state);
    if (!word_ok) return;
    CHECK(word_state.r[1] == 0xC3D4A1B2u);
    CHECK(word_state.r[2] == 0xA1B2C3D4u);
    CHECK(word_state.t);
}

static void test_xtrct_extracts_middle_32_bits_of_rm_rn_pair() {
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x11223344u;
    state.r[2] = 0xAABBCCDDu;
    state.t = true;
    state.pr = 0xDEAD2000u;

    const bool ok = execute_words({0x212D, 0x000B, 0x0009}, state);
    if (!ok) return;
    CHECK(state.r[1] == 0xCCDD1122u);
    CHECK(state.r[2] == 0xAABBCCDDu);
    CHECK(state.t);
}

int main() {
    test_decoder_patterns();
    test_extensions_preserve_t_and_use_low_byte_or_word();
    test_swap_byte_and_word();
    test_xtrct_extracts_middle_32_bits_of_rm_rn_pair();
    if (failures) {
        std::cerr << failures << " SH-4 data manipulation assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 data manipulation assertions passed\n";
    return 0;
}
