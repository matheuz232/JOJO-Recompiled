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
    if (!run) std::cerr << "reference executor error: " << run.detail << '\n';
    return static_cast<bool>(run);
}

static void test_decoder_recognizes_fmov_register() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF35Cu, 0).op != Sh4Op::unsupported); // FMOV FR5,FR3
    CHECK(jojo::decode_sh4(0xF72Cu, 0).op != Sh4Op::unsupported); // FMOV DR2,XD6 when SZ=1
}

static void test_single_precision_copy_preserves_bits() {
    jojo::Sh4ReferenceState state{};
    state.fr[5] = 0x7FC12345u;
    state.fr[3] = 0xDEADBEEFu;

    const bool ok = execute_words({
        0xF35Cu, // FMOV FR5,FR3
    }, state);
    if (!ok) return;

    CHECK(state.fr[3] == 0x7FC12345u);
    CHECK(state.fr[5] == 0x7FC12345u);
}

static void test_sz_selects_pair_and_extended_banks() {
    jojo::Sh4ReferenceState state{};
    state.fr[2] = 0x11111111u;
    state.fr[3] = 0x22222222u;
    state.xf[6] = 0xAAAAAAAAu;
    state.xf[7] = 0xBBBBBBBBu;

    const bool ok = execute_words({
        0xF3FDu, // FSCHG -> SZ=1
        0xF72Cu, // FMOV DR2,XD6
        0xF47Cu, // FMOV XD6,DR4
    }, state, 0x8C020000u);
    if (!ok) return;

    CHECK(state.xf[6] == 0x11111111u);
    CHECK(state.xf[7] == 0x22222222u);
    CHECK(state.fr[4] == 0x11111111u);
    CHECK(state.fr[5] == 0x22222222u);
    CHECK(state.fr[2] == 0x11111111u);
    CHECK(state.fr[3] == 0x22222222u);
}

static void test_same_encoding_is_single_register_when_sz_is_clear() {
    jojo::Sh4ReferenceState state{};
    state.fr[2] = 0x12345678u;
    state.fr[7] = 0x87654321u;
    state.xf[6] = 0xCAFEBABEu;
    state.xf[7] = 0x0BADF00Du;

    const bool ok = execute_words({
        0xF72Cu, // FMOV FR2,FR7 while SZ=0
    }, state, 0x8C030000u);
    if (!ok) return;

    CHECK(state.fr[7] == 0x12345678u);
    CHECK(state.xf[6] == 0xCAFEBABEu);
    CHECK(state.xf[7] == 0x0BADF00Du);
}

int main() {
    test_decoder_recognizes_fmov_register();
    test_single_precision_copy_preserves_bits();
    test_sz_selects_pair_and_extended_banks();
    test_same_encoding_is_single_register_when_sz_is_clear();
    if (failures) {
        std::cerr << failures << " SH-4 FMOV register assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FMOV register assertions passed\n";
    return 0;
}
