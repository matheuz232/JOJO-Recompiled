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
    CHECK(cfg.value.unsupported_sites.empty());
    if (!cfg.value.unsupported_sites.empty()) return false;

    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return false;

    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 16u);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_recognizes_mode_switches() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xFBFDu, 0).op != Sh4Op::unsupported); // FRCHG
    CHECK(jojo::decode_sh4(0xF3FDu, 0).op != Sh4Op::unsupported); // FSCHG
}

static void test_frchg_toggles_fpscr_and_preserves_physical_banks() {
    jojo::Sh4ReferenceState state{};
    state.fr[2] = 0x11111111u;
    state.xf[2] = 0x22222222u;

    bool ok = execute_words({
        0xFBFDu, // FRCHG -> alternate physical bank becomes active
        0x02FAu, // STS FPSCR,R2
    }, state);
    if (!ok) return;

    CHECK(state.r[2] == 0x00240001u);
    CHECK(state.fr[2] == 0x22222222u);
    CHECK(state.xf[2] == 0x11111111u);

    ok = execute_words({
        0xFBFDu, // FRCHG -> original physical bank becomes active again
        0x03FAu, // STS FPSCR,R3
    }, state, 0x8C020000u);
    if (!ok) return;

    CHECK(state.r[3] == 0x00040001u);
    CHECK(state.fr[2] == 0x11111111u);
    CHECK(state.xf[2] == 0x22222222u);
}

static void test_fschg_toggles_sz_without_swapping_banks() {
    jojo::Sh4ReferenceState state{};
    state.fr[4] = 0xAAAAAAAAu;
    state.xf[4] = 0xBBBBBBBBu;

    const bool ok = execute_words({
        0xF3FDu, // FSCHG -> SZ=1
        0x01FAu, // STS FPSCR,R1
        0xF3FDu, // FSCHG -> SZ=0
        0x02FAu, // STS FPSCR,R2
    }, state, 0x8C030000u);
    if (!ok) return;

    CHECK(state.r[1] == 0x00140001u);
    CHECK(state.r[2] == 0x00040001u);
    CHECK(state.fr[4] == 0xAAAAAAAAu);
    CHECK(state.xf[4] == 0xBBBBBBBBu);
}

int main() {
    test_decoder_recognizes_mode_switches();
    test_frchg_toggles_fpscr_and_preserves_physical_banks();
    test_fschg_toggles_sz_without_swapping_banks();
    if (failures) {
        std::cerr << failures << " SH-4 FPU mode switch assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU mode switch assertions passed\n";
    return 0;
}
