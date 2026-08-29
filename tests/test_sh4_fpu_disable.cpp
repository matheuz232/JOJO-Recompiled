#include "core/sh4_cfg.h"
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
    std::uint32_t base) {
    std::vector<std::uint8_t> bytes;
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    if (!cfg) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(cfg.error, cfg.detail);
    if (!cfg.value.unsupported_sites.empty()) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(
            jojo::ErrorCode::unsupported_format, "test contains unsupported SH-4 opcode");
    }
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    if (!ir) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(ir.error, ir.detail);
    return jojo::execute_sh4_ir_reference(ir.value, state, {}, 8u);
}

static void test_fd_vectors_before_fpu_state_mutation() {
    jojo::Sh4ReferenceState state{};
    state.sr = 1u << 15u; // FD
    state.vbr = 0x8C000000u;
    state.r[15] = 0x8CFF0000u;
    state.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[1] = std::bit_cast<std::uint32_t>(2.0f);
    const auto before = state.fr[0];

    const auto run = run_words({0xF010u}, state, 0x8C078000u); // FADD FR1,FR0
    CHECK(run);
    if (!run) return;
    CHECK(state.fr[0] == before);
    CHECK(state.expevt == 0x800u);
    CHECK(state.spc == 0x8C078000u);
    CHECK(state.sgr == 0x8CFF0000u);
    CHECK(state.pc == 0x8C000100u);
}

static void test_fd_in_delay_slot_uses_slot_fpu_disable_vector() {
    jojo::Sh4ReferenceState state{};
    state.sr = 1u << 15u;
    state.vbr = 0x8C000000u;
    state.r[15] = 0x8CFE0000u;
    state.fr[0] = std::bit_cast<std::uint32_t>(3.0f);
    state.fr[1] = std::bit_cast<std::uint32_t>(4.0f);
    const auto before = state.fr[0];

    const auto run = run_words({0xA000u, 0xF010u}, state, 0x8C078100u);
    CHECK(run);
    if (!run) return;
    CHECK(state.fr[0] == before);
    CHECK(state.expevt == 0x820u);
    CHECK(state.spc == 0x8C078100u);
    CHECK(state.sgr == 0x8CFE0000u);
    CHECK(state.pc == 0x8C000100u);
}

static void test_fd_blocks_fpul_and_fpscr_system_transfers() {
    jojo::Sh4ReferenceState fpul{};
    fpul.sr = 1u << 15u;
    fpul.vbr = 0x8C000000u;
    fpul.r[1] = 0xDEADBEEFu;
    fpul.fpul = 0x12345678u;
    const auto fpul_run = run_words({0x415Au}, fpul, 0x8C078200u); // LDS R1,FPUL
    CHECK(fpul_run);
    if (fpul_run) {
        CHECK(fpul.fpul == 0x12345678u);
        CHECK(fpul.expevt == 0x800u);
        CHECK(fpul.spc == 0x8C078200u);
    }

    jojo::Sh4ReferenceState fpscr{};
    fpscr.sr = 1u << 15u;
    fpscr.vbr = 0x8C000000u;
    fpscr.r[1] = 0u;
    const auto before = fpscr.fpscr;
    const auto fpscr_run = run_words({0x416Au}, fpscr, 0x8C078300u); // LDS R1,FPSCR
    CHECK(fpscr_run);
    if (fpscr_run) {
        CHECK(fpscr.fpscr == before);
        CHECK(fpscr.expevt == 0x800u);
        CHECK(fpscr.spc == 0x8C078300u);
    }
}

int main() {
    test_fd_vectors_before_fpu_state_mutation();
    test_fd_in_delay_slot_uses_slot_fpu_disable_vector();
    test_fd_blocks_fpul_and_fpscr_system_transfers();
    if (failures) {
        std::cerr << failures << " SH-4 FPU-disable assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU-disable assertions passed\n";
    return 0;
}
