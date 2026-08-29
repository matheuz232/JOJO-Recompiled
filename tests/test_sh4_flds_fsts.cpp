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

static void test_decoder_recognizes_flds_fsts() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF51Du, 0).op != Sh4Op::unsupported); // FLDS FR5,FPUL
    CHECK(jojo::decode_sh4(0xF90Du, 0).op != Sh4Op::unsupported); // FSTS FPUL,FR9
}

static void test_round_trip_preserves_all_32_bits() {
    jojo::Sh4ReferenceState state{};
    state.fr[5] = 0x7FC12345u; // NaN payload: these instructions copy bits, not values.

    const bool ok = execute_words({
        0xF51Du, // FLDS FR5,FPUL
        0xF90Du, // FSTS FPUL,FR9
    }, state);
    if (!ok) return;

    CHECK(state.fpul == 0x7FC12345u);
    CHECK(state.fr[9] == 0x7FC12345u);
}

static void test_fsts_targets_the_current_fr_bank() {
    jojo::Sh4ReferenceState state{};
    state.fr[3] = 0xDEADBEEFu;
    state.fpul = 0x12345678u;
    state.r[1] = 0x00240001u; // FR=1, DN=1, RM=1
    state.r[2] = 0x00040001u; // FR=0, DN=1, RM=1

    bool ok = execute_words({
        0x416Au, // LDS R1,FPSCR -> switch to XF physical bank
        0xF30Du, // FSTS FPUL,FR3
        0x426Au, // LDS R2,FPSCR -> return to original FR physical bank
    }, state);
    if (!ok) return;
    CHECK(state.fr[3] == 0xDEADBEEFu);

    ok = execute_words({
        0x416Au, // expose the bank written by FSTS again
    }, state, 0x8C020000u);
    if (!ok) return;
    CHECK(state.fr[3] == 0x12345678u);
}

int main() {
    test_decoder_recognizes_flds_fsts();
    test_round_trip_preserves_all_32_bits();
    test_fsts_targets_the_current_fr_bank();
    if (failures) {
        std::cerr << failures << " SH-4 FLDS/FSTS assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FLDS/FSTS assertions passed\n";
    return 0;
}
