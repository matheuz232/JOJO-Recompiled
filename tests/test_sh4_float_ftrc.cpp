#include "core/sh4_cfg.h"
#include "core/sh4_decoder.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <bit>
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
    CHECK(cfg.value.unsupported_sites.empty());
    if (!cfg.value.unsupported_sites.empty()) return false;

    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return false;

    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 16u);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_recognizes_float_and_ftrc() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF22Du, 0).op != Sh4Op::unsupported); // FLOAT FPUL,FR2
    CHECK(jojo::decode_sh4(0xF73Du, 0).op != Sh4Op::unsupported); // FTRC FR7,FPUL
}

static void test_float_converts_signed_fpul_to_fr() {
    jojo::Sh4ReferenceState state{};
    state.fpul = std::bit_cast<std::uint32_t>(std::int32_t{-3});

    const bool ok = execute_words({
        0xF22Du, // FLOAT FPUL,FR2
        0x000Bu,
        0x0009u,
    }, state);
    if (!ok) return;

    CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(-3.0f));
    CHECK(state.fpul == std::bit_cast<std::uint32_t>(std::int32_t{-3}));
}

static void test_ftrc_truncates_toward_zero() {
    jojo::Sh4ReferenceState positive{};
    positive.fr[7] = std::bit_cast<std::uint32_t>(3.75f);
    const bool positive_ok = execute_words({
        0xF73Du, // FTRC FR7,FPUL
        0x000Bu,
        0x0009u,
    }, positive);
    if (positive_ok) {
        CHECK(std::bit_cast<std::int32_t>(positive.fpul) == 3);
    }

    jojo::Sh4ReferenceState negative{};
    negative.fr[7] = std::bit_cast<std::uint32_t>(-3.75f);
    const bool negative_ok = execute_words({
        0xF73Du,
        0x000Bu,
        0x0009u,
    }, negative);
    if (negative_ok) {
        CHECK(std::bit_cast<std::int32_t>(negative.fpul) == -3);
    }
}

int main() {
    test_decoder_recognizes_float_and_ftrc();
    test_float_converts_signed_fpul_to_fr();
    test_ftrc_truncates_toward_zero();
    if (failures) {
        std::cerr << failures << " SH-4 FLOAT/FTRC assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FLOAT/FTRC assertions passed\n";
    return 0;
}
