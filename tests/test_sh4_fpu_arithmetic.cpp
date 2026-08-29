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

static jojo::Result<jojo::Sh4ReferenceRunResult> run_words(
    const std::vector<std::uint16_t>& words,
    jojo::Sh4ReferenceState& state,
    std::uint32_t base = 0x8C050000u) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2u);
    for (const auto word : words) append_word(bytes, word);

    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    if (!cfg) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(cfg.error, cfg.detail);
    }
    if (!cfg.value.unsupported_sites.empty()) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(
            jojo::ErrorCode::unsupported_format,
            "test program contains an unsupported SH-4 instruction");
    }

    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    if (!ir) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(ir.error, ir.detail);
    }
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

static void test_decoder_recognizes_binary_arithmetic() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF010u, 0).op == Sh4Op::fadd); // FADD FR1,FR0
    CHECK(jojo::decode_sh4(0xF231u, 0).op == Sh4Op::fsub); // FSUB FR3,FR2
    CHECK(jojo::decode_sh4(0xF452u, 0).op == Sh4Op::fmul); // FMUL FR5,FR4
    CHECK(jojo::decode_sh4(0xF673u, 0).op == Sh4Op::fdiv); // FDIV FR7,FR6
}

static void test_decoder_recognizes_fmac() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF23Eu, 0).op != Sh4Op::unsupported); // FMAC FR0,FR3,FR2
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

    const bool ok = execute_words({
        0xF010u, // FADD FR1,FR0
        0xF231u, // FSUB FR3,FR2
        0xF452u, // FMUL FR5,FR4
        0xF673u, // FDIV FR7,FR6
    }, state);
    if (!ok) return;

    CHECK(state.fr[0] == std::bit_cast<std::uint32_t>(3.75f));
    CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(6.0f));
    CHECK(state.fr[4] == std::bit_cast<std::uint32_t>(12.0f));
    CHECK(state.fr[6] == std::bit_cast<std::uint32_t>(2.0f));
}

static void test_fmac_multiplies_fr0_and_source_then_adds_destination() {
    jojo::Sh4ReferenceState state{};
    state.fr[0] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(4.0f);

    const bool ok = execute_words({0xF23Eu}, state, 0x8C050800u); // FMAC FR0,FR3,FR2
    if (ok) CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(9.0f));
}

static void test_fmac_uses_one_final_rounding() {
    jojo::Sh4ReferenceState state{};
    state.fpscr &= ~0x3u; // RM=0, round to nearest/even.
    state.fr[0] = 0x434B9203u;
    state.fr[2] = 0xC3173550u;
    state.fr[3] = 0x3FD20847u;

    const bool ok = execute_words({0xF23Eu}, state, 0x8C050C00u);
    if (ok) {
        CHECK(state.fr[2] == 0x4336D367u);
        CHECK(state.fr[2] != 0x4336D366u); // FMUL then FADD would double-round here.
    }
}

static void test_rounding_mode_selects_nearest_or_toward_zero() {
    jojo::Sh4ReferenceState toward_zero{}; // Reset FPSCR uses RM=1.
    toward_zero.fr[0] = 0x3F800000u; // 1.0
    toward_zero.fr[1] = 0x33C00000u; // 3 * 2^-25
    if (execute_words({0xF010u}, toward_zero, 0x8C051000u)) {
        CHECK(toward_zero.fr[0] == 0x3F800000u);
    }

    jojo::Sh4ReferenceState nearest{};
    nearest.fpscr &= ~0x3u; // RM=0, round to nearest/even.
    nearest.fr[0] = 0x3F800000u;
    nearest.fr[1] = 0x33C00000u;
    if (execute_words({0xF010u}, nearest, 0x8C052000u)) {
        CHECK(nearest.fr[0] == 0x3F800001u);
    }

    jojo::Sh4ReferenceState negative_toward_zero{};
    negative_toward_zero.fr[0] = 0xBF800000u; // -1.0
    negative_toward_zero.fr[1] = 0xB3C00000u; // -3 * 2^-25
    if (execute_words({0xF010u}, negative_toward_zero, 0x8C052800u)) {
        CHECK(negative_toward_zero.fr[0] == 0xBF800000u);
    }
}

static void test_dn_flushes_a_subnormal_result_to_zero() {
    jojo::Sh4ReferenceState state{}; // Reset FPSCR has DN=1.
    state.fr[0] = 0x00800000u; // Smallest positive normal.
    state.fr[1] = std::bit_cast<std::uint32_t>(0.5f);

    const bool ok = execute_words({0xF012u}, state, 0x8C053000u); // FMUL FR1,FR0
    if (ok) CHECK(state.fr[0] == 0x00000000u);

    jojo::Sh4ReferenceState negative{};
    negative.fr[0] = 0x80800000u; // Smallest negative normal.
    negative.fr[1] = std::bit_cast<std::uint32_t>(0.5f);
    if (execute_words({0xF012u}, negative, 0x8C053800u)) {
        CHECK(negative.fr[0] == 0x80000000u);
    }
}

static void test_unsupported_modes_fail_without_changing_destination() {
    jojo::Sh4ReferenceState double_precision{};
    double_precision.fpscr |= 0x00080000u; // PR=1.
    double_precision.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    double_precision.fr[1] = std::bit_cast<std::uint32_t>(2.0f);
    const auto double_run = run_words({0xF010u}, double_precision, 0x8C054000u);
    CHECK(!double_run);
    CHECK(double_precision.fr[0] == std::bit_cast<std::uint32_t>(1.0f));

    jojo::Sh4ReferenceState division_by_zero{};
    division_by_zero.fr[1] = 0x00000000u;
    division_by_zero.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    const auto divide_run = run_words({0xF213u}, division_by_zero, 0x8C055000u);
    CHECK(!divide_run);
    CHECK(division_by_zero.fr[2] == std::bit_cast<std::uint32_t>(1.0f));
}

static void test_fmac_rejects_double_precision_without_mutation() {
    jojo::Sh4ReferenceState state{};
    state.fpscr |= 0x00080000u; // PR=1.
    state.fr[0] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(4.0f);
    const auto before = state.fr[2];

    const auto run = run_words({0xF23Eu}, state, 0x8C056000u);
    CHECK(!run);
    CHECK(state.fr[2] == before);
}

int main() {
    test_decoder_recognizes_binary_arithmetic();
    test_decoder_recognizes_fmac();
    test_normal_single_precision_operations();
    test_fmac_multiplies_fr0_and_source_then_adds_destination();
    test_fmac_uses_one_final_rounding();
    test_rounding_mode_selects_nearest_or_toward_zero();
    test_dn_flushes_a_subnormal_result_to_zero();
    test_unsupported_modes_fail_without_changing_destination();
    test_fmac_rejects_double_precision_without_mutation();
    if (failures) {
        std::cerr << failures << " SH-4 FPU arithmetic assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU arithmetic assertions passed\n";
    return 0;
}