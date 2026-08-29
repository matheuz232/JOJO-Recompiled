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
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    CHECK(cfg); if (!cfg) return false;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return false;
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, memory, 16);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_patterns_and_scaled_displacements() {
    using jojo::Sh4Op;
    auto i = jojo::decode_sh4(0xC003u, 0);
    CHECK(i.op == Sh4Op::movb_store_gbr_disp && i.displacement == 3);
    i = jojo::decode_sh4(0xC102u, 0);
    CHECK(i.op == Sh4Op::movw_store_gbr_disp && i.displacement == 4);
    i = jojo::decode_sh4(0xC202u, 0);
    CHECK(i.op == Sh4Op::movl_store_gbr_disp && i.displacement == 8);
    i = jojo::decode_sh4(0xC403u, 0);
    CHECK(i.op == Sh4Op::movb_load_gbr_disp && i.displacement == 3);
    i = jojo::decode_sh4(0xC502u, 0);
    CHECK(i.op == Sh4Op::movw_load_gbr_disp && i.displacement == 4);
    i = jojo::decode_sh4(0xC602u, 0);
    CHECK(i.op == Sh4Op::movl_load_gbr_disp && i.displacement == 8);
}

static void test_gbr_stores_use_r0_and_little_endian() {
    std::vector<std::uint8_t> memory(32, 0);
    jojo::Sh4ReferenceState state{};
    state.gbr = 0x9000u;
    state.r[0] = 0xAABBCC80u;
    state.pr = 0xDEAD0000u;
    CHECK(execute_words({0xC001u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}));
    CHECK(memory[1] == 0x80u);

    state.r[0] = 0xAABBCC80u;
    state.pr = 0xDEAD0000u;
    CHECK(execute_words({0xC101u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}));
    CHECK(memory[2] == 0x80u && memory[3] == 0xCCu);

    state.r[0] = 0xAABBCC80u;
    state.pr = 0xDEAD0000u;
    CHECK(execute_words({0xC201u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}));
    CHECK(memory[4] == 0x80u && memory[5] == 0xCCu && memory[6] == 0xBBu && memory[7] == 0xAAu);
}

static void test_gbr_loads_sign_extend_byte_and_word() {
    std::vector<std::uint8_t> memory(32, 0);
    memory[1] = 0x80u;
    memory[2] = 0x80u; memory[3] = 0xCCu;
    memory[4] = 0x78u; memory[5] = 0x56u; memory[6] = 0x34u; memory[7] = 0x12u;

    jojo::Sh4ReferenceState state{};
    state.gbr = 0xA000u;
    state.pr = 0xDEAD1000u;
    CHECK(execute_words({0xC401u, 0x000Bu, 0x0009u}, state, {0xA000u, memory}));
    CHECK(state.r[0] == 0xFFFFFF80u);

    state.pr = 0xDEAD1000u;
    CHECK(execute_words({0xC501u, 0x000Bu, 0x0009u}, state, {0xA000u, memory}));
    CHECK(state.r[0] == 0xFFFFCC80u);

    state.pr = 0xDEAD1000u;
    CHECK(execute_words({0xC601u, 0x000Bu, 0x0009u}, state, {0xA000u, memory}));
    CHECK(state.r[0] == 0x12345678u);
}

static void test_gbr_byte_immediate_logic() {
    std::vector<std::uint8_t> memory(32, 0);
    memory[5] = 0xA5u;
    jojo::Sh4ReferenceState state{};
    state.gbr = 0xC000u;
    state.r[0] = 5u;
    state.pr = 0xDEAD2800u;

    CHECK(execute_words({0xCC0Fu, 0x000Bu, 0x0009u}, state, {0xC000u, memory}));
    CHECK(!state.t);
    CHECK(memory[5] == 0xA5u);

    state.pr = 0xDEAD2800u;
    CHECK(execute_words({0xCD0Fu, 0x000Bu, 0x0009u}, state, {0xC000u, memory}));
    CHECK(memory[5] == 0x05u);

    state.pr = 0xDEAD2800u;
    CHECK(execute_words({0xCEF0u, 0x000Bu, 0x0009u}, state, {0xC000u, memory}));
    CHECK(memory[5] == 0xF5u);

    state.pr = 0xDEAD2800u;
    CHECK(execute_words({0xCF0Au, 0x000Bu, 0x0009u}, state, {0xC000u, memory}));
    CHECK(memory[5] == 0xFFu);
}

static void test_gbr_access_is_bounded() {
    std::vector<std::uint8_t> memory(8, 0);
    jojo::Sh4ReferenceState state{};
    state.gbr = 0xB004u;
    state.r[0] = 0x12345678u;
    state.pr = 0xDEAD2000u;

    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xC201u); // MOV.L R0,@(4,GBR) -> 0xB008, outside map
    append_word(bytes, 0x000Bu);
    append_word(bytes, 0x0009u);
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x8C010000u, 0x8C010000u);
    CHECK(cfg); if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return;
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {0xB000u, memory}, 8);
    CHECK(!run);
    if (!run) CHECK(run.error == jojo::ErrorCode::invalid_argument);
}

int main() {
    test_decoder_patterns_and_scaled_displacements();
    test_gbr_stores_use_r0_and_little_endian();
    test_gbr_loads_sign_extend_byte_and_word();
    test_gbr_byte_immediate_logic();
    test_gbr_access_is_bounded();
    if (failures) {
        std::cerr << failures << " SH-4 GBR memory assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 GBR memory assertions passed\n";
    return 0;
}
