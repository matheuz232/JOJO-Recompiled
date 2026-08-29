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

static void test_decoder_recognizes_fpul_transfer_forms() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0x415Au, 0).op != Sh4Op::unsupported); // LDS R1,FPUL
    CHECK(jojo::decode_sh4(0x025Au, 0).op != Sh4Op::unsupported); // STS FPUL,R2
    CHECK(jojo::decode_sh4(0x4356u, 0).op != Sh4Op::unsupported); // LDS.L @R3+,FPUL
    CHECK(jojo::decode_sh4(0x4452u, 0).op != Sh4Op::unsupported); // STS.L FPUL,@-R4
}

static void test_decoder_recognizes_fpscr_transfer_forms() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0x416Au, 0).op != Sh4Op::unsupported); // LDS R1,FPSCR
    CHECK(jojo::decode_sh4(0x026Au, 0).op != Sh4Op::unsupported); // STS FPSCR,R2
    CHECK(jojo::decode_sh4(0x4366u, 0).op != Sh4Op::unsupported); // LDS.L @R3+,FPSCR
    CHECK(jojo::decode_sh4(0x4462u, 0).op != Sh4Op::unsupported); // STS.L FPSCR,@-R4
}

static void test_register_forms_round_trip_fpul() {
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x89ABCDEFu;
    state.r[2] = 0x11223344u;

    const bool ok = execute_words({
        0x415Au,
        0x025Au,
        0x000Bu,
        0x0009u,
    }, state);
    if (!ok) return;

    CHECK(state.r[1] == 0x89ABCDEFu);
    CHECK(state.r[2] == 0x89ABCDEFu);
}

static void test_memory_forms_round_trip_fpul_little_endian() {
    std::vector<std::uint8_t> memory(32, 0u);
    memory[0] = 0x78u;
    memory[1] = 0x56u;
    memory[2] = 0x34u;
    memory[3] = 0x12u;

    jojo::Sh4ReferenceState state{};
    state.r[3] = 0x9000u;
    state.r[4] = 0x9010u;

    const bool ok = execute_words({
        0x4356u,
        0x4452u,
        0x000Bu,
        0x0009u,
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(state.r[3] == 0x9004u);
    CHECK(state.r[4] == 0x900Cu);
    CHECK(memory[12] == 0x78u);
    CHECK(memory[13] == 0x56u);
    CHECK(memory[14] == 0x34u);
    CHECK(memory[15] == 0x12u);
}

int main() {
    test_decoder_recognizes_fpul_transfer_forms();
    test_decoder_recognizes_fpscr_transfer_forms();
    test_register_forms_round_trip_fpul();
    test_memory_forms_round_trip_fpul_little_endian();
    if (failures) {
        std::cerr << failures << " SH-4 FPUL/FPSCR transfer assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPUL/FPSCR transfer assertions passed\n";
    return 0;
}
