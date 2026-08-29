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

static jojo::Result<jojo::Sh4ReferenceRunResult> run_word(
    std::uint16_t word,
    jojo::Sh4ReferenceState& state,
    std::uint32_t base = 0x8C070000u) {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    if (!cfg) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(cfg.error, cfg.detail);
    if (!cfg.value.unsupported_sites.empty()) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(
            jojo::ErrorCode::unsupported_format,
            "test program contains an unsupported SH-4 instruction");
    }
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    if (!ir) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(ir.error, ir.detail);
    return jojo::execute_sh4_ir_reference(ir.value, state, {}, 8u);
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

static void test_decoder_recognizes_unary_fpu_ops() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF24Du, 0).op == Sh4Op::fneg);
    CHECK(jojo::decode_sh4(0xF35Du, 0).op == Sh4Op::fabs);
    CHECK(jojo::decode_sh4(0xF46Du, 0).op == Sh4Op::fsqrt);
}

static void test_fneg_and_fabs_only_change_the_sign_bit() {
    jojo::Sh4ReferenceState state{};
    state.fr[2] = std::bit_cast<std::uint32_t>(1.5f);
    auto neg = run_word(0xF24Du, state);
    CHECK(neg);
    if (neg) CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(-1.5f));

    state.fr[3] = std::bit_cast<std::uint32_t>(-2.5f);
    auto abs = run_word(0xF35Du, state, 0x8C071000u);
    CHECK(abs);
    if (abs) CHECK(state.fr[3] == std::bit_cast<std::uint32_t>(2.5f));

    state.fr[3] = 0xFFC12345u;
    abs = run_word(0xF35Du, state, 0x8C073000u);
    CHECK(abs);
    if (abs) CHECK(state.fr[3] == 0x7FC12345u);
}

static void test_fneg_preserves_existing_fpscr_cause_and_flags() {
    constexpr std::uint32_t kCauseZ = 1u << 15u;
    constexpr std::uint32_t kFlagZ = 1u << 5u;
    jojo::Sh4ReferenceState state{};
    state.fpscr |= kCauseZ | kFlagZ;
    state.fr[2] = std::bit_cast<std::uint32_t>(1.0f);
    const auto run = run_word(0xF24Du, state, 0x8C073800u);
    CHECK(run);
    if (!run) return;
    CHECK((state.fpscr & kCauseZ) != 0u);
    CHECK((state.fpscr & kFlagZ) != 0u);
}

static void test_single_fsqrt_and_masked_invalid_operation() {
    jojo::Sh4ReferenceState state{};
    state.fr[4] = std::bit_cast<std::uint32_t>(9.0f);
    auto run = run_word(0xF46Du, state, 0x8C074000u);
    CHECK(run);
    if (run) CHECK(state.fr[4] == std::bit_cast<std::uint32_t>(3.0f));

    constexpr std::uint32_t kCauseV = 1u << 16u;
    constexpr std::uint32_t kFlagV = 1u << 6u;
    jojo::Sh4ReferenceState negative{};
    negative.fr[4] = std::bit_cast<std::uint32_t>(-1.0f);
    run = run_word(0xF46Du, negative, 0x8C075000u);
    CHECK(run);
    if (!run) return;
    CHECK(std::isnan(std::bit_cast<float>(negative.fr[4])));
    CHECK((negative.fpscr & kCauseV) != 0u);
    CHECK((negative.fpscr & kFlagV) != 0u);
}

static void test_double_precision_fneg_fabs_and_fsqrt() {
    jojo::Sh4ReferenceState state{};
    state.fpscr = (state.fpscr & ~0x3u) | 0x00080000u; // PR=1, SZ=0.

    set_dr(state, 2, 2.0);
    auto run = run_word(0xF24Du, state, 0x8C076000u); // FNEG DR2
    CHECK(run);
    if (run) CHECK(get_dr(state, 2) == -2.0);

    set_dr(state, 4, -3.0);
    run = run_word(0xF45Du, state, 0x8C076100u); // FABS DR4
    CHECK(run);
    if (run) CHECK(get_dr(state, 4) == 3.0);

    set_dr(state, 6, 16.0);
    run = run_word(0xF66Du, state, 0x8C076200u); // FSQRT DR6
    CHECK(run);
    if (run) CHECK(get_dr(state, 6) == 4.0);
}

int main() {
    test_decoder_recognizes_unary_fpu_ops();
    test_fneg_and_fabs_only_change_the_sign_bit();
    test_fneg_preserves_existing_fpscr_cause_and_flags();
    test_single_fsqrt_and_masked_invalid_operation();
    test_double_precision_fneg_fabs_and_fsqrt();
    if (failures) {
        std::cerr << failures << " SH-4 FPU unary assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU unary assertions passed\n";
    return 0;
}
