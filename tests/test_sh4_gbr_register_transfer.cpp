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
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, memory, 32);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_patterns() {
    using jojo::Sh4Op;

    auto i = jojo::decode_sh4(0x431E, 0x1000); // LDC R3,GBR
    CHECK(i.op == Sh4Op::ldc_gbr_reg);
    CHECK(i.rm == 3);

    i = jojo::decode_sh4(0x0512, 0x1002); // STC GBR,R5
    CHECK(i.op == Sh4Op::stc_gbr_reg);
    CHECK(i.rn == 5);

    i = jojo::decode_sh4(0x4417, 0x1004); // LDC.L @R4+,GBR
    CHECK(i.op == Sh4Op::ldc_gbr_postinc);
    CHECK(i.rm == 4);

    i = jojo::decode_sh4(0x4613, 0x1006); // STC.L GBR,@-R6
    CHECK(i.op == Sh4Op::stc_gbr_predec);
    CHECK(i.rn == 6);
}

static void test_register_forms_round_trip_gbr() {
    jojo::Sh4ReferenceState state{};
    state.r[3] = 0x8C200000u;
    state.pr = 0xDEAD0000u;

    const bool ok = execute_words({
        0x431E, // LDC R3,GBR
        0x0512, // STC GBR,R5
        0x000B,
        0x0009,
    }, state, {});
    if (!ok) return;

    CHECK(state.gbr == 0x8C200000u);
    CHECK(state.r[5] == 0x8C200000u);
}

static void test_memory_forms_are_little_endian_and_update_address_registers() {
    std::vector<std::uint8_t> memory(32, 0);
    memory[4] = 0x78;
    memory[5] = 0x56;
    memory[6] = 0x34;
    memory[7] = 0x12;

    jojo::Sh4ReferenceState state{};
    state.r[4] = 0x9004u;
    state.r[6] = 0x9010u;
    state.pr = 0xDEAD1000u;

    const bool ok = execute_words({
        0x4417, // LDC.L @R4+,GBR
        0x4613, // STC.L GBR,@-R6
        0x000B,
        0x0009,
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(state.gbr == 0x12345678u);
    CHECK(state.r[4] == 0x9008u);
    CHECK(state.r[6] == 0x900Cu);
    CHECK(memory[12] == 0x78u);
    CHECK(memory[13] == 0x56u);
    CHECK(memory[14] == 0x34u);
    CHECK(memory[15] == 0x12u);
}

static void test_memory_forms_fail_outside_mapped_memory_without_partial_update() {
    std::vector<std::uint8_t> memory(4, 0);
    jojo::Sh4ReferenceState state{};
    state.gbr = 0xCAFEBABEu;
    state.r[4] = 0xA003u;
    state.r[6] = 0xA003u;
    state.pr = 0xDEAD2000u;

    std::vector<std::uint8_t> code;
    append_word(code, 0x4417); // LDC.L @R4+,GBR crosses memory end
    append_word(code, 0x000B);
    append_word(code, 0x0009);

    const auto cfg = jojo::build_sh4_cfg(code, 0x8C010000u, 0x8C010000u);
    CHECK(cfg);
    if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return;

    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {0xA000u, memory}, 8);
    CHECK(!run);
    if (!run) CHECK(run.error == jojo::ErrorCode::invalid_argument);
    CHECK(state.gbr == 0xCAFEBABEu);
    CHECK(state.r[4] == 0xA003u);
}

int main() {
    test_decoder_patterns();
    test_register_forms_round_trip_gbr();
    test_memory_forms_are_little_endian_and_update_address_registers();
    test_memory_forms_fail_outside_mapped_memory_without_partial_update();
    if (failures) {
        std::cerr << failures << " SH-4 GBR register transfer assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 GBR register transfer assertions passed\n";
    return 0;
}
