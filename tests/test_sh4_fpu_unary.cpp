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

static void test_decoder_recognizes_unary_fpu_ops() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF24Du, 0).op != Sh4Op::unsupported); // FNEG FR2
    CHECK(jojo::decode_sh4(0xF35Du, 0).op != Sh4Op::unsupported); // FABS FR3
    CHECK(jojo::decode_sh4(0xF46Du, 0).op != Sh4Op::unsupported); // FSQRT FR4
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

    state.fr[2] = 0x00000000u;
    neg = run_word(0xF24Du, state, 0x8C072000u);
    CHECK(neg);
    if (neg) CHECK(state.fr[2] == 0x80000000u);

    state.fr[3] = 0xFFC12345u;
    abs = run_word(0xF35Du, state, 0x8C073000u);
    CHECK(abs);
    if (abs) CHECK(state.fr[3] == 0x7FC12345u);
}

static void test_fsqrt_computes_single_precision_square_root() {
    jojo::Sh4ReferenceState state{};
    state.fr[4] = std::bit_cast<std::uint32_t>(9.0f);
    const auto run = run_word(0xF46Du, state, 0x8C074000u);
    CHECK(run);
    if (run) CHECK(state.fr[4] == std::bit_cast<std::uint32_t>(3.0f));
}

static void test_fsqrt_rejects_unimplemented_exception_and_double_paths_without_mutation() {
    jojo::Sh4ReferenceState state{};
    state.fr[4] = std::bit_cast<std::uint32_t>(-1.0f);
    const auto before_negative = state.fr[4];
    const auto negative = run_word(0xF46Du, state, 0x8C075000u);
    CHECK(!negative);
    CHECK(state.fr[4] == before_negative);

    state.fpscr |= 0x00080000u; // PR=1
    state.fr[4] = std::bit_cast<std::uint32_t>(16.0f);
    const auto before_double = state.fr[4];
    const auto double_mode = run_word(0xF46Du, state, 0x8C076000u);
    CHECK(!double_mode);
    CHECK(state.fr[4] == before_double);
}

int main() {
    test_decoder_recognizes_unary_fpu_ops();
    test_fneg_and_fabs_only_change_the_sign_bit();
    test_fsqrt_computes_single_precision_square_root();
    test_fsqrt_rejects_unimplemented_exception_and_double_paths_without_mutation();
    if (failures) {
        std::cerr << failures << " SH-4 FPU unary assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU unary assertions passed\n";
    return 0;
}
