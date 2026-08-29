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
                          jojo::Sh4ReferenceMemoryView memory = {},
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

    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, memory, 16u);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_recognizes_fpscr_transfer_forms() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0x416Au, 0).op != Sh4Op::unsupported); // LDS R1,FPSCR
    CHECK(jojo::decode_sh4(0x026Au, 0).op != Sh4Op::unsupported); // STS FPSCR,R2
    CHECK(jojo::decode_sh4(0x4366u, 0).op != Sh4Op::unsupported); // LDS.L @R3+,FPSCR
    CHECK(jojo::decode_sh4(0x4462u, 0).op != Sh4Op::unsupported); // STS.L FPSCR,@-R4
}

static void test_default_fpscr_and_reserved_bits() {
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0xFFE40001u;

    const bool ok = execute_words({
        0x026Au, // STS FPSCR,R2 -- architectural reset value
        0x416Au, // LDS R1,FPSCR -- high reserved bits must read back as zero
        0x036Au, // STS FPSCR,R3
        0x000Bu,
        0x0009u,
    }, state);
    if (!ok) return;

    CHECK(state.r[2] == 0x00040001u);
    CHECK(state.r[3] == 0x00240001u);
}

static void test_fr_bit_switches_and_preserves_physical_banks() {
    jojo::Sh4ReferenceState state{};
    state.fr[3] = 0xDEADBEEFu;
    state.r[1] = 0x00240001u; // FR=1, DN=1, RM=1
    state.r[2] = 0x00040001u; // FR=0, DN=1, RM=1

    bool ok = execute_words({
        0x416Au, // LDS R1,FPSCR -> expose the other physical bank as FR
        0xF39Du, // FLDI1 FR3 -> write 1.0f into that bank
        0x426Au, // LDS R2,FPSCR -> return to original bank
        0x000Bu,
        0x0009u,
    }, state);
    if (!ok) return;
    CHECK(state.fr[3] == 0xDEADBEEFu);

    ok = execute_words({
        0x416Au, // expose bank written by FLDI1 again
        0x000Bu,
        0x0009u,
    }, state, {}, 0x8C020000u);
    if (!ok) return;
    CHECK(state.fr[3] == 0x3F800000u);
}

static void test_memory_forms_round_trip_masked_fpscr() {
    std::vector<std::uint8_t> memory(32, 0u);
    memory[0] = 0x01u;
    memory[1] = 0x00u;
    memory[2] = 0xE4u;
    memory[3] = 0xFFu;

    jojo::Sh4ReferenceState state{};
    state.r[3] = 0x9000u;
    state.r[4] = 0x9010u;

    const bool ok = execute_words({
        0x4366u, // LDS.L @R3+,FPSCR
        0x4462u, // STS.L FPSCR,@-R4
        0x000Bu,
        0x0009u,
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(state.r[3] == 0x9004u);
    CHECK(state.r[4] == 0x900Cu);
    CHECK(memory[12] == 0x01u);
    CHECK(memory[13] == 0x00u);
    CHECK(memory[14] == 0x24u);
    CHECK(memory[15] == 0x00u);
}

int main() {
    test_decoder_recognizes_fpscr_transfer_forms();
    test_default_fpscr_and_reserved_bits();
    test_fr_bit_switches_and_preserves_physical_banks();
    test_memory_forms_round_trip_masked_fpscr();
    if (failures) {
        std::cerr << failures << " SH-4 FPSCR/bank assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPSCR/bank assertions passed\n";
    return 0;
}
