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
    std::uint32_t base = 0x8C060000u) {
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

static void test_decoder_recognizes_fcmp() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF014u, 0).op == Sh4Op::fcmp_eq);
    CHECK(jojo::decode_sh4(0xF235u, 0).op == Sh4Op::fcmp_gt);
}

static void test_single_precision_compare_updates_t() {
    jojo::Sh4ReferenceState state{};
    state.fr[0] = std::bit_cast<std::uint32_t>(2.5f);
    state.fr[1] = std::bit_cast<std::uint32_t>(2.5f);
    auto equal = run_word(0xF014u, state);
    CHECK(equal);
    if (equal) CHECK(state.t);

    state.fr[2] = std::bit_cast<std::uint32_t>(4.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(3.0f);
    auto greater = run_word(0xF235u, state, 0x8C061000u);
    CHECK(greater);
    if (greater) CHECK(state.t);

    state.fr[3] = std::bit_cast<std::uint32_t>(5.0f);
    auto not_greater = run_word(0xF235u, state, 0x8C062000u);
    CHECK(not_greater);
    if (not_greater) CHECK(!state.t);
}

static void test_double_precision_compare_updates_t() {
    jojo::Sh4ReferenceState state{};
    state.fpscr |= 0x00080000u; // PR=1.
    set_dr(state, 0, 6.5);
    set_dr(state, 2, 6.5);
    state.t = false;
    auto equal = run_word(0xF024u, state, 0x8C063000u); // FCMP/EQ DR2,DR0
    CHECK(equal);
    if (equal) CHECK(state.t);

    set_dr(state, 0, 7.0);
    set_dr(state, 2, 6.0);
    state.t = false;
    auto greater = run_word(0xF025u, state, 0x8C063100u); // FCMP/GT DR2,DR0
    CHECK(greater);
    if (greater) CHECK(state.t);
}

static void test_odd_double_register_encoding_vectors_as_illegal_without_mutating_t() {
    jojo::Sh4ReferenceState state{};
    state.fpscr |= 0x00080000u; // PR=1.
    state.vbr = 0x8C000000u;
    state.t = true;
    set_dr(state, 0, 1.0);
    state.fr[1] = std::bit_cast<std::uint32_t>(1.0f);
    const auto run = run_word(0xF014u, state, 0x8C063200u); // odd source selector in PR=1.
    CHECK(run);
    if (!run) return;
    CHECK(state.t);
    CHECK(state.expevt == 0x180u);
    CHECK(state.spc == 0x8C063200u);
    CHECK(state.pc == 0x8C000100u);
}

int main() {
    test_decoder_recognizes_fcmp();
    test_single_precision_compare_updates_t();
    test_double_precision_compare_updates_t();
    test_odd_double_register_encoding_vectors_as_illegal_without_mutating_t();
    if (failures) {
        std::cerr << failures << " SH-4 FPU compare assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU compare assertions passed\n";
    return 0;
}
