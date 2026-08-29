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
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return false;
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 32);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_patterns() {
    using jojo::Sh4Op;
    auto i = jojo::decode_sh4(0x612C, 0x8C010000u); // EXTU.B R2,R1
    CHECK(i.op == Sh4Op::extu_b && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x634D, 0x8C010002u); // EXTU.W R4,R3
    CHECK(i.op == Sh4Op::extu_w && i.rn == 3 && i.rm == 4);
    i = jojo::decode_sh4(0x656E, 0x8C010004u); // EXTS.B R6,R5
    CHECK(i.op == Sh4Op::exts_b && i.rn == 5 && i.rm == 6);
    i = jojo::decode_sh4(0x678F, 0x8C010006u); // EXTS.W R8,R7
    CHECK(i.op == Sh4Op::exts_w && i.rn == 7 && i.rm == 8);
}

static void test_extension_execution() {
    jojo::Sh4ReferenceState state{};
    state.r[2] = 0x1234FF80u;
    state.r[4] = 0x89ABCDEFu;
    state.r[6] = 0x00000080u;
    state.r[8] = 0x00008001u;
    state.pr = 0xDEAD4000u;

    const bool ok = execute_words({
        0x612C, // EXTU.B R2,R1 -> 0x80
        0x634D, // EXTU.W R4,R3 -> 0xCDEF
        0x656E, // EXTS.B R6,R5 -> 0xFFFFFF80
        0x678F, // EXTS.W R8,R7 -> 0xFFFF8001
        0x000B, // RTS
        0x0009, // delay slot
    }, state);
    if (!ok) return;

    CHECK(state.r[1] == 0x00000080u);
    CHECK(state.r[3] == 0x0000CDEFu);
    CHECK(state.r[5] == 0xFFFFFF80u);
    CHECK(state.r[7] == 0xFFFF8001u);
    CHECK(state.r[2] == 0x1234FF80u);
    CHECK(state.r[4] == 0x89ABCDEFu);
    CHECK(state.r[6] == 0x00000080u);
    CHECK(state.r[8] == 0x00008001u);
    CHECK(state.pc == 0xDEAD4000u);
}

int main() {
    test_decoder_patterns();
    test_extension_execution();
    if (failures) {
        std::cerr << failures << " SH-4 extension assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 extension assertions passed\n";
    return 0;
}
