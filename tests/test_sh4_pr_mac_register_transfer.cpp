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

static void put16(std::vector<std::uint8_t>& memory, std::size_t offset, std::uint16_t value) {
    memory[offset] = static_cast<std::uint8_t>(value);
    memory[offset + 1] = static_cast<std::uint8_t>(value >> 8u);
}

static void put32(std::vector<std::uint8_t>& memory, std::size_t offset, std::uint32_t value) {
    memory[offset] = static_cast<std::uint8_t>(value);
    memory[offset + 1] = static_cast<std::uint8_t>(value >> 8u);
    memory[offset + 2] = static_cast<std::uint8_t>(value >> 16u);
    memory[offset + 3] = static_cast<std::uint8_t>(value >> 24u);
}

static void test_decoder_patterns() {
    using jojo::Sh4Op;

    CHECK(jojo::decode_sh4(0x430A, 0).op == Sh4Op::lds_mach_reg);
    CHECK(jojo::decode_sh4(0x051A, 0).op == Sh4Op::sts_macl_reg);
    CHECK(jojo::decode_sh4(0x462A, 0).op == Sh4Op::lds_pr_reg);

    CHECK(jojo::decode_sh4(0x4406, 0).op == Sh4Op::lds_mach_postinc);
    CHECK(jojo::decode_sh4(0x4516, 0).op == Sh4Op::lds_macl_postinc);
    CHECK(jojo::decode_sh4(0x4626, 0).op == Sh4Op::lds_pr_postinc);

    CHECK(jojo::decode_sh4(0x4702, 0).op == Sh4Op::sts_mach_predec);
    CHECK(jojo::decode_sh4(0x4812, 0).op == Sh4Op::sts_macl_predec);
    CHECK(jojo::decode_sh4(0x4922, 0).op == Sh4Op::sts_pr_predec);

    auto i = jojo::decode_sh4(0x052A, 0); // STS PR,R5
    CHECK(i.op == Sh4Op::sts_pr_reg);
    CHECK(i.rn == 5);
}

static void test_register_forms_round_trip_special_registers() {
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x11112222u;
    state.r[2] = 0x33334444u;
    state.r[3] = 0x55556666u;
    state.pr = 0xDEAD0000u;

    const bool ok = execute_words({
        0x410A, // LDS R1,MACH
        0x421A, // LDS R2,MACL
        0x432A, // LDS R3,PR
        0x040A, // STS MACH,R4
        0x051A, // STS MACL,R5
        0x062A, // STS PR,R6
        0x000B,
        0x0009,
    }, state, {});
    if (!ok) return;

    CHECK(state.mach == 0x11112222u);
    CHECK(state.macl == 0x33334444u);
    CHECK(state.pr == 0x55556666u);
    CHECK(state.r[4] == 0x11112222u);
    CHECK(state.r[5] == 0x33334444u);
    CHECK(state.r[6] == 0x55556666u);
}

static void test_memory_forms_load_and_store_little_endian() {
    std::vector<std::uint8_t> memory(64, 0);
    put32(memory, 0, 0x11223344u);
    put32(memory, 4, 0x55667788u);
    put32(memory, 8, 0x99AABBCCu);

    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x9000u;
    state.r[2] = 0x9004u;
    state.r[3] = 0x9008u;
    state.r[7] = 0x9020u;
    state.r[8] = 0x9028u;
    state.r[9] = 0x9030u;
    state.pr = 0xDEAD1000u;

    const bool ok = execute_words({
        0x4106, // LDS.L @R1+,MACH
        0x4216, // LDS.L @R2+,MACL
        0x4326, // LDS.L @R3+,PR
        0x4702, // STS.L MACH,@-R7
        0x4812, // STS.L MACL,@-R8
        0x4922, // STS.L PR,@-R9
        0x000B,
        0x0009,
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(state.mach == 0x11223344u);
    CHECK(state.macl == 0x55667788u);
    CHECK(state.pr == 0x99AABBCCu);
    CHECK(state.r[1] == 0x9004u && state.r[2] == 0x9008u && state.r[3] == 0x900Cu);
    CHECK(state.r[7] == 0x901Cu && state.r[8] == 0x9024u && state.r[9] == 0x902Cu);

    CHECK(memory[28] == 0x44u && memory[29] == 0x33u && memory[30] == 0x22u && memory[31] == 0x11u);
    CHECK(memory[36] == 0x88u && memory[37] == 0x77u && memory[38] == 0x66u && memory[39] == 0x55u);
    CHECK(memory[44] == 0xCCu && memory[45] == 0xBBu && memory[46] == 0xAAu && memory[47] == 0x99u);
}

static void test_mac_l_accumulates_signed_64_and_saturates_to_48_bits() {
    std::vector<std::uint8_t> memory(32, 0);
    put32(memory, 0, 0xFFFFFFFEu); // -2
    put32(memory, 4, 3u);

    jojo::Sh4ReferenceState state{};
    state.r[2] = 0xA000u;
    state.r[1] = 0xA004u;
    state.mach = 0u;
    state.macl = 10u;
    state.pr = 0xDEAD3000u;
    CHECK(execute_words({0x012Fu, 0x000Bu, 0x0009u}, state, {0xA000u, memory}, 0x8C013000u));
    CHECK(state.mach == 0u && state.macl == 4u);
    CHECK(state.r[2] == 0xA004u && state.r[1] == 0xA008u);

    put32(memory, 0, 2u);
    put32(memory, 4, 2u);
    state.r[2] = 0xA000u;
    state.r[1] = 0xA004u;
    state.mach = 0x00007FFFu;
    state.macl = 0xFFFFFFFEu;
    state.sr = 0x00000002u; // S=1 saturation mode
    state.pr = 0xDEAD3000u;
    CHECK(execute_words({0x012Fu, 0x000Bu, 0x0009u}, state, {0xA000u, memory}, 0x8C013000u));
    CHECK(state.mach == 0x00007FFFu && state.macl == 0xFFFFFFFFu);
}

static void test_mac_w_accumulates_signed_64_and_saturates_mac_l() {
    std::vector<std::uint8_t> memory(16, 0);
    put16(memory, 0, static_cast<std::uint16_t>(0xFFFEu)); // -2
    put16(memory, 2, 3u);

    jojo::Sh4ReferenceState state{};
    state.r[2] = 0xB000u;
    state.r[1] = 0xB002u;
    state.mach = 0u;
    state.macl = 10u;
    state.pr = 0xDEAD4000u;
    CHECK(execute_words({0x412Fu, 0x000Bu, 0x0009u}, state, {0xB000u, memory}, 0x8C014000u));
    CHECK(state.mach == 0u && state.macl == 4u);
    CHECK(state.r[2] == 0xB002u && state.r[1] == 0xB004u);

    put16(memory, 0, 2u);
    put16(memory, 2, 2u);
    state.r[2] = 0xB000u;
    state.r[1] = 0xB002u;
    state.mach = 0x12345678u;
    state.macl = 0x7FFFFFFEu;
    state.sr = 0x00000002u; // S=1
    state.pr = 0xDEAD4000u;
    CHECK(execute_words({0x412Fu, 0x000Bu, 0x0009u}, state, {0xB000u, memory}, 0x8C014000u));
    CHECK(state.macl == 0x7FFFFFFFu);
    CHECK(state.mach == 1u);
}

static void test_failed_memory_transfer_does_not_partially_update_state() {
    std::vector<std::uint8_t> memory(4, 0);
    jojo::Sh4ReferenceState state{};
    state.mach = 0xAAAAAAAAu;
    state.r[1] = 0xA003u;
    state.pr = 0xDEAD2000u;

    std::vector<std::uint8_t> code;
    append_word(code, 0x4106); // LDS.L @R1+,MACH crosses memory end
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
    CHECK(state.mach == 0xAAAAAAAAu);
    CHECK(state.r[1] == 0xA003u);
}

int main() {
    test_decoder_patterns();
    test_register_forms_round_trip_special_registers();
    test_memory_forms_load_and_store_little_endian();
    test_mac_l_accumulates_signed_64_and_saturates_to_48_bits();
    test_mac_w_accumulates_signed_64_and_saturates_mac_l();
    test_failed_memory_transfer_does_not_partially_update_state();
    if (failures) {
        std::cerr << failures << " SH-4 PR/MAC transfer assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 PR/MAC transfer assertions passed\n";
    return 0;
}
