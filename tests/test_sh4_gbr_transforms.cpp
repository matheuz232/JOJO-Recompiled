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
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, memory, 32);
    CHECK(run); return static_cast<bool>(run);
}

static void test_decoder_patterns() {
    using jojo::Sh4Op;
    auto i = jojo::decode_sh4(0xC003, 0); // MOV.B R0,@(3,GBR)
    CHECK(i.op == Sh4Op::movb_store_gbr && i.rn == 0 && i.rm == 0 && i.displacement == 3);
    i = jojo::decode_sh4(0xC102, 0); // MOV.W R0,@(2,GBR)
    CHECK(i.op == Sh4Op::movw_store_gbr && i.displacement == 4);
    i = jojo::decode_sh4(0xC203, 0); // MOV.L R0,@(3,GBR)
    CHECK(i.op == Sh4Op::movl_store_gbr && i.displacement == 12);
    i = jojo::decode_sh4(0xC403, 0); // MOV.B @(3,GBR),R0
    CHECK(i.op == Sh4Op::movb_load_gbr && i.displacement == 3);
    i = jojo::decode_sh4(0xC502, 0); // MOV.W @(2,GBR),R0
    CHECK(i.op == Sh4Op::movw_load_gbr && i.displacement == 4);
    i = jojo::decode_sh4(0xC603, 0); // MOV.L @(3,GBR),R0
    CHECK(i.op == Sh4Op::movl_load_gbr && i.displacement == 12);

    CHECK(jojo::decode_sh4(0x6128, 0).op == Sh4Op::swap_b);
    CHECK(jojo::decode_sh4(0x6129, 0).op == Sh4Op::swap_w);
    CHECK(jojo::decode_sh4(0x212D, 0).op == Sh4Op::xtrct);
    CHECK(jojo::decode_sh4(0x612C, 0).op == Sh4Op::extu_b);
    CHECK(jojo::decode_sh4(0x612D, 0).op == Sh4Op::extu_w);
    CHECK(jojo::decode_sh4(0x612E, 0).op == Sh4Op::exts_b);
    CHECK(jojo::decode_sh4(0x612F, 0).op == Sh4Op::exts_w);
}

static void test_gbr_memory_pipeline() {
    std::vector<std::uint8_t> memory(64, 0);
    jojo::Sh4ReferenceState state{};
    state.gbr = 0x9000u;
    state.r[0] = 0xAABBCC80u;
    state.pr = 0xDEAD0000u;

    const bool ok = execute_words({
        0xC003, // byte store GBR+3
        0xC403, // byte load GBR+3 -> R0 signed
        0xC102, // word store GBR+4 using current R0
        0xC502, // word load GBR+4 -> R0 signed
        0xE078, // R0 = 0x78
        0xC203, // long store GBR+12
        0xC603, // long load GBR+12
        0x000B, 0x0009,
    }, state, {0x9000u, memory});
    if (!ok) return;

    CHECK(memory[3] == 0x80u);
    CHECK(memory[4] == 0x80u && memory[5] == 0xFFu);
    CHECK(memory[12] == 0x78u && memory[13] == 0u && memory[14] == 0u && memory[15] == 0u);
    CHECK(state.r[0] == 0x78u);
}

static void test_transform_pipeline() {
    jojo::Sh4ReferenceState state{};
    state.r[2] = 0xAABBCCDDu;
    state.r[3] = 0x11223344u;
    state.r[4] = 0xFFFFFF80u;
    state.r[5] = 0xFFFFCC80u;
    state.pr = 0xDEAD1000u;

    const bool ok = execute_words({
        0x6128, // SWAP.B R2,R1 -> AABBDDCC
        0x6629, // SWAP.W R2,R6 -> CCDDAABB
        0x232D, // XTRCT R3,R2 -> 3344AABB
        0x674C, // EXTU.B R4,R7 -> 00000080
        0x685D, // EXTU.W R5,R8 -> 0000CC80
        0x694E, // EXTS.B R4,R9 -> FFFFFF80
        0x6A5F, // EXTS.W R5,R10 -> FFFFCC80
        0x000B, 0x0009,
    }, state, {});
    if (!ok) return;

    CHECK(state.r[1] == 0xAABBDDCCu);
    CHECK(state.r[6] == 0xCCDDAABBu);
    CHECK(state.r[2] == 0x3344AABBu);
    CHECK(state.r[7] == 0x00000080u);
    CHECK(state.r[8] == 0x0000CC80u);
    CHECK(state.r[9] == 0xFFFFFF80u);
    CHECK(state.r[10] == 0xFFFFCC80u);
}

int main() {
    test_decoder_patterns();
    test_gbr_memory_pipeline();
    test_transform_pipeline();
    if (failures) {
        std::cerr << failures << " SH-4 GBR/transform assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 GBR/transform assertions passed\n";
    return 0;
}
