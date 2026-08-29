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
                          jojo::Sh4ReferenceMemoryView memory,
                          std::uint32_t base = 0x8C040000u) {
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

    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, memory, 32u);
    CHECK(run);
    if (!run) std::cerr << "reference executor error: " << run.detail << '\n';
    return static_cast<bool>(run);
}

static void test_decoder_recognizes_memory_forms() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF31Au, 0).op != Sh4Op::unsupported); // FMOV.S FR1,@R3
    CHECK(jojo::decode_sh4(0xF248u, 0).op != Sh4Op::unsupported); // FMOV.S @R4,FR2
    CHECK(jojo::decode_sh4(0xF659u, 0).op != Sh4Op::unsupported); // FMOV.S @R5+,FR6
    CHECK(jojo::decode_sh4(0xF87Bu, 0).op != Sh4Op::unsupported); // FMOV.S FR7,@-R8
    CHECK(jojo::decode_sh4(0xFA96u, 0).op != Sh4Op::unsupported); // FMOV.S @(R0,R9),FR10
    CHECK(jojo::decode_sh4(0xFCB7u, 0).op != Sh4Op::unsupported); // FMOV.S FR11,@(R0,R12)
}

static void test_direct_load_and_store_preserve_bits() {
    std::vector<std::uint8_t> memory(32u, 0u);
    memory[8] = 0x78u;
    memory[9] = 0x56u;
    memory[10] = 0x34u;
    memory[11] = 0x12u;

    jojo::Sh4ReferenceState state{};
    state.r[3] = 0x9004u;
    state.r[4] = 0x9008u;
    state.fr[1] = 0xA1B2C3D4u;

    const bool ok = execute_words({
        0xF31Au, // FMOV.S FR1,@R3
        0xF248u, // FMOV.S @R4,FR2
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(memory[4] == 0xD4u && memory[5] == 0xC3u);
    CHECK(memory[6] == 0xB2u && memory[7] == 0xA1u);
    CHECK(state.fr[2] == 0x12345678u);
}

static void test_postincrement_and_predecrement() {
    std::vector<std::uint8_t> memory(32u, 0u);
    memory[0] = 0x44u;
    memory[1] = 0x33u;
    memory[2] = 0x22u;
    memory[3] = 0x11u;

    jojo::Sh4ReferenceState state{};
    state.r[5] = 0xA000u;
    state.r[8] = 0xA010u;
    state.fr[7] = 0x55667788u;

    const bool ok = execute_words({
        0xF659u, // FMOV.S @R5+,FR6
        0xF87Bu, // FMOV.S FR7,@-R8
    }, state, {0xA000u, memory}, 0x8C041000u);
    if (!ok) return;

    CHECK(state.fr[6] == 0x11223344u);
    CHECK(state.r[5] == 0xA004u);
    CHECK(state.r[8] == 0xA00Cu);
    CHECK(memory[12] == 0x88u && memory[13] == 0x77u);
    CHECK(memory[14] == 0x66u && memory[15] == 0x55u);
}

static void test_indexed_load_and_store() {
    std::vector<std::uint8_t> memory(32u, 0u);
    memory[8] = 0xEFu;
    memory[9] = 0xBEu;
    memory[10] = 0xADu;
    memory[11] = 0xDEu;

    jojo::Sh4ReferenceState state{};
    state.r[0] = 4u;
    state.r[9] = 0xB004u;
    state.r[12] = 0xB00Cu;
    state.fr[11] = 0xCAFEBABEu;

    const bool ok = execute_words({
        0xFA96u, // FMOV.S @(R0,R9),FR10
        0xFCB7u, // FMOV.S FR11,@(R0,R12)
    }, state, {0xB000u, memory}, 0x8C042000u);
    if (!ok) return;

    CHECK(state.fr[10] == 0xDEADBEEFu);
    CHECK(memory[16] == 0xBEu && memory[17] == 0xBAu);
    CHECK(memory[18] == 0xFEu && memory[19] == 0xCAu);
}

static void test_sz_pair_transfer_uses_xd_bank_and_little_endian_word_order() {
    std::vector<std::uint8_t> memory(32u, 0u);
    memory[0] = 0x44u;
    memory[1] = 0x33u;
    memory[2] = 0x22u;
    memory[3] = 0x11u;
    memory[4] = 0x88u;
    memory[5] = 0x77u;
    memory[6] = 0x66u;
    memory[7] = 0x55u;

    jojo::Sh4ReferenceState state{};
    state.r[4] = 0xC000u;
    state.r[5] = 0xC018u;

    const bool ok = execute_words({
        0xF3FDu, // FSCHG -> SZ=1
        0xF749u, // FMOV @R4+,XD6
        0xF57Bu, // FMOV XD6,@-R5
    }, state, {0xC000u, memory}, 0x8C043000u);
    if (!ok) return;

    CHECK(state.r[4] == 0xC008u);
    CHECK(state.r[5] == 0xC010u);
    CHECK(state.xf[6] == 0x55667788u);
    CHECK(state.xf[7] == 0x11223344u);
    CHECK(memory[16] == 0x44u && memory[17] == 0x33u);
    CHECK(memory[18] == 0x22u && memory[19] == 0x11u);
    CHECK(memory[20] == 0x88u && memory[21] == 0x77u);
    CHECK(memory[22] == 0x66u && memory[23] == 0x55u);
}

int main() {
    test_decoder_recognizes_memory_forms();
    test_direct_load_and_store_preserve_bits();
    test_postincrement_and_predecrement();
    test_indexed_load_and_store();
    test_sz_pair_transfer_uses_xd_bank_and_little_endian_word_order();
    if (failures) {
        std::cerr << failures << " SH-4 FMOV memory assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FMOV memory assertions passed\n";
    return 0;
}
