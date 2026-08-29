#include "core/sh4_cfg.h"
#include "core/sh4_decoder.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static jojo::Result<jojo::Sh4ReferenceRunResult> run_words(
    const std::vector<std::uint16_t>& words,
    jojo::Sh4ReferenceState& state,
    std::uint32_t base = 0x8C050000u) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2u);
    for (const auto word : words) append_word(bytes, word);

    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    if (!cfg) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(cfg.error, cfg.detail);
    if (!cfg.value.unsupported_sites.empty()) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(
            jojo::ErrorCode::unsupported_format,
            "test program contains an unsupported SH-4 instruction");
    }
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    if (!ir) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(ir.error, ir.detail);
    return jojo::execute_sh4_ir_reference(ir.value, state, {}, 16u);
}

static bool execute_words(const std::vector<std::uint16_t>& words,
                          jojo::Sh4ReferenceState& state,
                          std::uint32_t base = 0x8C050000u) {
    const auto run = run_words(words, state, base);
    CHECK(run);
    if (!run) std::cerr << "reference executor error: " << run.detail << '\n';
    return static_cast<bool>(run);
}

static void set_dr(jojo::Sh4ReferenceState& state, std::uint8_t even, double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    state.fr[even] = static_cast<std::uint32_t>(bits >> 32u);
    state.fr[even + 1u] = static_cast<std::uint32_t>(bits);
}

static double get_dr(const jojo::Sh4ReferenceState& state, std::uint8_t even) {
    const auto bits = (static_cast<std::uint64_t>(state.fr[even]) << 32u) |
                      static_cast<std::uint64_t>(state.fr[even + 1u]);
    return std::bit_cast<double>(bits);
}

static void test_decoder_recognizes_binary_arithmetic() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF010u, 0).op == Sh4Op::fadd);
    CHECK(jojo::decode_sh4(0xF231u, 0).op == Sh4Op::fsub);
    CHECK(jojo::decode_sh4(0xF452u, 0).op == Sh4Op::fmul);
    CHECK(jojo::decode_sh4(0xF673u, 0).op == Sh4Op::fdiv);
    CHECK(jojo::decode_sh4(0xF23Eu, 0).op == Sh4Op::fmac);
}

static void test_normal_single_precision_operations() {
    jojo::Sh4ReferenceState state{};
    state.fr[0] = std::bit_cast<std::uint32_t>(1.25f);
    state.fr[1] = std::bit_cast<std::uint32_t>(2.5f);
    state.fr[2] = std::bit_cast<std::uint32_t>(10.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(4.0f);
    state.fr[4] = std::bit_cast<std::uint32_t>(1.5f);
    state.fr[5] = std::bit_cast<std::uint32_t>(8.0f);
    state.fr[6] = std::bit_cast<std::uint32_t>(9.0f);
    state.fr[7] = std::bit_cast<std::uint32_t>(4.5f);

    if (!execute_words({0xF010u, 0xF231u, 0xF452u, 0xF673u}, state)) return;
    CHECK(state.fr[0] == std::bit_cast<std::uint32_t>(3.75f));
    CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(6.0f));
    CHECK(state.fr[4] == std::bit_cast<std::uint32_t>(12.0f));
    CHECK(state.fr[6] == std::bit_cast<std::uint32_t>(2.0f));
}

static void test_fmac_and_one_final_rounding() {
    jojo::Sh4ReferenceState state{};
    state.fr[0] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(4.0f);
    if (execute_words({0xF23Eu}, state, 0x8C050800u)) {
        CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(9.0f));
    }

    jojo::Sh4ReferenceState fused{};
    fused.fpscr &= ~0x3u;
    fused.fr[0] = 0x434B9203u;
    fused.fr[2] = 0xC3173550u;
    fused.fr[3] = 0x3FD20847u;
    if (execute_words({0xF23Eu}, fused, 0x8C050C00u)) {
        CHECK(fused.fr[2] == 0x4336D367u);
        CHECK(fused.fr[2] != 0x4336D366u);
    }
}

static void test_rounding_mode_selects_nearest_or_toward_zero() {
    jojo::Sh4ReferenceState toward_zero{};
    toward_zero.fr[0] = 0x3F800000u;
    toward_zero.fr[1] = 0x33C00000u;
    if (execute_words({0xF010u}, toward_zero, 0x8C051000u)) {
        CHECK(toward_zero.fr[0] == 0x3F800000u);
    }

    jojo::Sh4ReferenceState nearest{};
    nearest.fpscr &= ~0x3u;
    nearest.fr[0] = 0x3F800000u;
    nearest.fr[1] = 0x33C00000u;
    if (execute_words({0xF010u}, nearest, 0x8C052000u)) {
        CHECK(nearest.fr[0] == 0x3F800001u);
    }
}

static void test_dn_flushes_a_subnormal_result_to_zero() {
    jojo::Sh4ReferenceState state{};
    state.fr[0] = 0x00800000u;
    state.fr[1] = std::bit_cast<std::uint32_t>(0.5f);
    if (execute_words({0xF012u}, state, 0x8C053000u)) CHECK(state.fr[0] == 0x00000000u);

    jojo::Sh4ReferenceState negative{};
    negative.fr[0] = 0x80800000u;
    negative.fr[1] = std::bit_cast<std::uint32_t>(0.5f);
    if (execute_words({0xF012u}, negative, 0x8C053800u)) CHECK(negative.fr[0] == 0x80000000u);
}

static void test_double_precision_binary_arithmetic() {
    jojo::Sh4ReferenceState state{};
    state.fpscr = (state.fpscr & ~0x3u) | 0x00080000u; // PR=1, RM=nearest.
    set_dr(state, 0, 1.25);
    set_dr(state, 2, 2.5);
    const auto run = run_words({0xF020u}, state, 0x8C054000u); // FADD DR2,DR0
    CHECK(run);
    if (run) CHECK(get_dr(state, 0) == 3.75);
}

static void test_masked_divide_by_zero_commits_infinity_and_fpscr() {
    constexpr std::uint32_t kCauseZ = 1u << 15u;
    constexpr std::uint32_t kFlagZ = 1u << 5u;
    jojo::Sh4ReferenceState state{};
    state.fr[1] = 0x00000000u;
    state.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    const auto run = run_words({0xF213u}, state, 0x8C055000u); // FDIV FR1,FR2
    CHECK(run);
    if (!run) return;
    CHECK(std::isinf(std::bit_cast<float>(state.fr[2])));
    CHECK((state.fpscr & kCauseZ) != 0u);
    CHECK((state.fpscr & kFlagZ) != 0u);
}

static void test_fmac_in_double_mode_raises_illegal_instruction_without_mutation() {
    jojo::Sh4ReferenceState state{};
    state.fpscr |= 0x00080000u; // PR=1; FMAC is PR=0 only.
    state.vbr = 0x8C000000u;
    state.fr[0] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(4.0f);
    const auto before = state.fr[2];
    const auto run = run_words({0xF23Eu}, state, 0x8C056000u);
    CHECK(run);
    if (!run) return;
    CHECK(state.fr[2] == before);
    CHECK(state.expevt == 0x180u);
    CHECK(state.spc == 0x8C056000u);
    CHECK(state.pc == 0x8C000100u);
}

int main() {
    test_decoder_recognizes_binary_arithmetic();
    test_normal_single_precision_operations();
    test_fmac_and_one_final_rounding();
    test_rounding_mode_selects_nearest_or_toward_zero();
    test_dn_flushes_a_subnormal_result_to_zero();
    test_double_precision_binary_arithmetic();
    test_masked_divide_by_zero_commits_infinity_and_fpscr();
    test_fmac_in_double_mode_raises_illegal_instruction_without_mutation();
    if (failures) {
        std::cerr << failures << " SH-4 FPU arithmetic assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU arithmetic assertions passed\n";
    return 0;
}
