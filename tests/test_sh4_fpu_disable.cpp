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

static void test_fd_vectors_before_fpu_state_mutation() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xF010u); // FADD FR1,FR0
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x8C078000u, 0x8C078000u);
    CHECK(cfg); if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return;

    jojo::Sh4ReferenceState state{};
    state.sr = 1u << 15u; // FD
    state.vbr = 0x8C000000u;
    state.r[15] = 0x8CFF0000u;
    state.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[1] = std::bit_cast<std::uint32_t>(2.0f);
    const auto before = state.fr[0];

    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 4u);
    CHECK(run);
    if (!run) return;
    CHECK(state.fr[0] == before);
    CHECK(state.expevt == 0x800u);
    CHECK(state.spc == 0x8C078000u);
    CHECK(state.sgr == 0x8CFF0000u);
    CHECK(state.pc == 0x8C000100u);
}

int main() {
    test_fd_vectors_before_fpu_state_mutation();
    if (failures) {
        std::cerr << failures << " SH-4 FPU-disable assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU-disable assertions passed\n";
    return 0;
}
