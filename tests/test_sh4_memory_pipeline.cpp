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
    CHECK(jojo::decode_sh4(0x2120, 0).op == Sh4Op::movb_store);
    CHECK(jojo::decode_sh4(0x2121, 0).op == Sh4Op::movw_store);
    CHECK(jojo::decode_sh4(0x2122, 0).op == Sh4Op::movl_store);
    CHECK(jojo::decode_sh4(0x6310, 0).op == Sh4Op::movb_load);
    CHECK(jojo::decode_sh4(0x6311, 0).op == Sh4Op::movw_load);
    CHECK(jojo::decode_sh4(0x6312, 0).op == Sh4Op::movl_load);
    CHECK(jojo::decode_sh4(0x2124, 0).op == Sh4Op::movb_store_predec);
    CHECK(jojo::decode_sh4(0x2125, 0).op == Sh4Op::movw_store_predec);
    CHECK(jojo::decode_sh4(0x2126, 0).op == Sh4Op::movl_store_predec);
    CHECK(jojo::decode_sh4(0x6314, 0).op == Sh4Op::movb_load_postinc);
    CHECK(jojo::decode_sh4(0x6315, 0).op == Sh4Op::movw_load_postinc);
    CHECK(jojo::decode_sh4(0x6316, 0).op == Sh4Op::movl_load_postinc);
}

static void test_basic_store_load_and_sign_extension() {
    std::vector<std::uint8_t> memory(32, 0);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x9000;
    state.r[2] = 0xAABBCC80u;
    state.r[4] = 0x9004;
    state.r[6] = 0x9008;
    state.pr = 0xDEAD0000u;
    const bool ok = execute_words({
        0x2120, // MOV.B R2,@R1
        0x6310, // MOV.B @R1,R3
        0x2421, // MOV.W R2,@R4
        0x6541, // MOV.W @R4,R5
        0x2622, // MOV.L R2,@R6
        0x6762, // MOV.L @R6,R7
        0x000B,
        0x0009,
    }, state, {0x9000, memory});
    if (!ok) return;

    CHECK(memory[0] == 0x80u);
    CHECK(state.r[3] == 0xFFFFFF80u);
    CHECK(memory[4] == 0x80u && memory[5] == 0xCCu);
    CHECK(state.r[5] == 0xFFFFCC80u);
    CHECK(memory[8] == 0x80u && memory[9] == 0xCCu && memory[10] == 0xBBu && memory[11] == 0xAAu);
    CHECK(state.r[7] == 0xAABBCC80u);
}

static void test_predecrement_and_postincrement() {
    std::vector<std::uint8_t> memory(32, 0);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x9010;
    state.r[2] = 0x11223344u;
    state.r[4] = 0x9008;
    state.pr = 0xDEAD1000u;

    const bool ok = execute_words({
        0x2126, // MOV.L R2,@-R1 -> address 0x900C
        0x6546, // MOV.L @R4+,R5 -> read at 0x9008, R4 += 4
        0x000B,
        0x0009,
    }, state, {0x9000, memory});
    if (!ok) return;

    CHECK(state.r[1] == 0x900Cu);
    CHECK(memory[12] == 0x44u && memory[13] == 0x33u && memory[14] == 0x22u && memory[15] == 0x11u);
    CHECK(state.r[4] == 0x900Cu);
    CHECK(state.r[5] == 0u);
}

static void test_postincrement_alias_does_not_increment() {
    std::vector<std::uint8_t> memory(16, 0);
    memory[4] = 0x78;
    memory[5] = 0x56;
    memory[6] = 0x34;
    memory[7] = 0x12;
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0xA004;
    state.pr = 0xDEAD2000u;
    const bool ok = execute_words({
        0x6116, // MOV.L @R1+,R1: loaded data wins, no +4
        0x000B,
        0x0009,
    }, state, {0xA000, memory});
    if (!ok) return;
    CHECK(state.r[1] == 0x12345678u);
}

static void test_predecrement_alias_stores_old_source() {
    std::vector<std::uint8_t> memory(16, 0);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0xB008;
    state.pr = 0xDEAD3000u;
    const bool ok = execute_words({
        0x2116, // MOV.L R1,@-R1
        0x000B,
        0x0009,
    }, state, {0xB000, memory});
    if (!ok) return;
    CHECK(state.r[1] == 0xB004u);
    CHECK(memory[4] == 0x08u && memory[5] == 0xB0u && memory[6] == 0x00u && memory[7] == 0x00u);
}

static void test_out_of_range_store_fails() {
    std::vector<std::uint8_t> memory(4, 0);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0xC003;
    state.r[2] = 0x12345678u;
    state.pr = 0xDEAD4000u;

    std::vector<std::uint8_t> code;
    append_word(code, 0x2122); // MOV.L R2,@R1, crosses memory end
    append_word(code, 0x000B);
    append_word(code, 0x0009);
    const auto cfg = jojo::build_sh4_cfg(code, 0x8C010000u, 0x8C010000u);
    CHECK(cfg);
    if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return;
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {0xC000, memory}, 8);
    CHECK(!run);
    if (!run) CHECK(run.error == jojo::ErrorCode::invalid_argument);
}

int main() {
    test_decoder_patterns();
    test_basic_store_load_and_sign_extension();
    test_predecrement_and_postincrement();
    test_postincrement_alias_does_not_increment();
    test_predecrement_alias_stores_old_source();
    test_out_of_range_store_fails();
    if (failures) {
        std::cerr << failures << " SH-4 memory assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 memory assertions passed\n";
    return 0;
}
