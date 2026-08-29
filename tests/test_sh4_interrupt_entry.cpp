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

static jojo::Result<jojo::Sh4ReferenceRunResult> execute_words(
    const std::vector<std::uint16_t>& words,
    jojo::Sh4ReferenceState& state,
    jojo::Sh4ReferenceMemoryView memory = {},
    std::uint32_t base = 0x8C010000u) {
    std::vector<std::uint8_t> bytes;
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    if (!cfg) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(cfg.error, cfg.detail);
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    if (!ir) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(ir.error, ir.detail);
    return jojo::execute_sh4_ir_reference(ir.value, state, memory, 32u);
}

static void test_irl13_enters_interrupt_vector_and_saves_state() {
    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010100u;
    state.r[0] = 0x11111111u;
    state.r_bank[0] = 0x22222222u;
    state.r[15] = 0x8CFF0000u;
    state.vbr = 0x8C000000u;
    state.sr = 0x00000000u;
    state.t = true;

    const auto accepted = jojo::accept_sh4_irl_interrupt(state, 13u);
    CHECK(accepted);
    if (!accepted) return;
    CHECK(accepted.value);
    CHECK(state.spc == 0x8C010100u);
    CHECK(state.ssr == 0x00000001u);
    CHECK(state.sgr == 0x8CFF0000u);
    CHECK(state.intevt == 0x00000240u);
    CHECK(state.pc == 0x8C000600u);
    CHECK((state.sr & 0x70000000u) == 0x70000000u); // MD | RB | BL
    CHECK(state.r[0] == 0x22222222u);
    CHECK(state.r_bank[0] == 0x11111111u);
}

static void test_imask_rejects_equal_or_lower_irq_level() {
    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010100u;
    state.vbr = 0x8C000000u;
    state.sr = 13u << 4u;

    const auto accepted = jojo::accept_sh4_irl_interrupt(state, 13u);
    CHECK(accepted);
    if (!accepted) return;
    CHECK(!accepted.value);
    CHECK(state.pc == 0x8C010100u);
    CHECK(state.spc == 0u);
}

static void test_bl_masks_irl_interrupts() {
    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010100u;
    state.sr = 0x10000000u; // BL

    const auto accepted = jojo::accept_sh4_irl_interrupt(state, 15u);
    CHECK(accepted);
    if (accepted) CHECK(!accepted.value);
    CHECK(state.pc == 0x8C010100u);
}

static void test_invalid_irl_level_is_rejected() {
    jojo::Sh4ReferenceState state{};
    const auto accepted = jojo::accept_sh4_irl_interrupt(state, 0u);
    CHECK(!accepted);
    if (!accepted) CHECK(accepted.error == jojo::ErrorCode::invalid_argument);
}

static void test_rte_restores_ssr_spc_t_and_register_bank_after_delay_slot() {
    jojo::Sh4IrProgram program{};
    program.entry_address = 0x8C000600u;

    jojo::Sh4IrBlock block{};
    block.start_address = program.entry_address;
    block.exit = jojo::Sh4IrExit::return_exception;

    jojo::Sh4IrInstruction rte{};
    rte.op = jojo::Sh4IrOp::return_exception;
    rte.source_address = 0x8C000600u;
    block.ops.push_back(rte);

    jojo::Sh4IrInstruction delay{};
    delay.op = jojo::Sh4IrOp::nop;
    delay.source_address = 0x8C000602u;
    delay.in_delay_slot = true;
    block.ops.push_back(delay);
    program.blocks.push_back(block);

    jojo::Sh4ReferenceState state{};
    state.sr = 0x700000F0u;
    state.r[0] = 0x22222222u;
    state.r_bank[0] = 0x11111111u;
    state.ssr = 0x00000021u;
    state.spc = 0x8C010100u;

    jojo::Sh4ReferenceMemoryView memory{};
    const auto run = jojo::execute_sh4_ir_reference(program, state, memory, 4u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.operations_executed == 2u);
    CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::left_program);
    CHECK(state.sr == 0x00000020u);
    CHECK(state.t);
    CHECK(state.pc == 0x8C010100u);
    CHECK(state.r[0] == 0x11111111u);
    CHECK(state.r_bank[0] == 0x22222222u);
}

static void test_trapa_enters_general_vector_and_swaps_to_privileged_bank() {
    jojo::Sh4ReferenceState state{};
    state.vbr = 0x8C000000u;
    state.r[0] = 0x11111111u;
    state.r_bank[0] = 0x22222222u;
    state.r[15] = 0x8CFF0000u;
    state.sr = 0x00000020u;
    state.t = true;

    const auto run = execute_words({0xC37Fu, 0x0009u}, state, {}, 0x8C020000u);
    CHECK(run);
    if (!run) return;
    CHECK(state.spc == 0x8C020002u);
    CHECK(state.ssr == 0x00000021u);
    CHECK(state.sgr == 0x8CFF0000u);
    CHECK(state.tra == 0x000001FCu);
    CHECK(state.expevt == 0x00000160u);
    CHECK(state.pc == 0x8C000100u);
    CHECK((state.sr & 0x70000000u) == 0x70000000u);
    CHECK(state.r[0] == 0x22222222u && state.r_bank[0] == 0x11111111u);
}

static void test_ldc_stc_sr_and_bank_registers_switch_active_bank() {
    jojo::Sh4ReferenceState state{};
    state.sr = 0x60000000u; // privileged, bank 1 active
    state.r[0] = 0xAAAA0000u;
    state.r_bank[0] = 0xBBBB0000u;
    state.r[8] = 0x40000001u; // privileged, bank 0, T=1
    state.pr = 0xDEAD5000u;

    const auto run = execute_words({
        0x480Eu, // LDC R8,SR
        0x0902u, // STC SR,R9
        0x488Eu, // LDC R8,R0_BANK
        0x0A82u, // STC R0_BANK,R10
        0x000Bu,
        0x0009u,
    }, state, {}, 0x8C025000u);
    CHECK(run);
    if (!run) return;
    CHECK(state.r[0] == 0xBBBB0000u);
    CHECK(state.r_bank[0] == 0x40000001u);
    CHECK(state.r[9] == 0x40000001u);
    CHECK(state.r[10] == 0x40000001u);
    CHECK(state.t);
}

static void test_tas_movca_cache_mmu_and_sleep_system_effects() {
    std::vector<std::uint8_t> memory(64, 0);
    jojo::Sh4ReferenceState state{};
    state.sr = 0x40000000u; // privileged mode for LDTLB/SLEEP
    state.r[0] = 0xAABBCCDDu;
    state.r[1] = 0x9004u;
    state.pr = 0xDEAD6000u;

    auto run = execute_words({0x411Bu, 0x000Bu, 0x0009u}, state, {0x9000u, memory}, 0x8C026000u);
    CHECK(run);
    CHECK(state.t);
    CHECK(memory[4] == 0x80u);

    state.pr = 0xDEAD6000u;
    run = execute_words({0x01C3u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}, 0x8C026000u);
    CHECK(run);
    CHECK(memory[4] == 0xDDu && memory[5] == 0xCCu && memory[6] == 0xBBu && memory[7] == 0xAAu);
    CHECK(state.last_system_event == jojo::Sh4ReferenceSystemEvent::movca_l);
    CHECK(state.system_event_address == 0x9004u);

    state.pr = 0xDEAD6000u;
    run = execute_words({0x0193u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}, 0x8C026000u);
    CHECK(run);
    CHECK(state.last_system_event == jojo::Sh4ReferenceSystemEvent::ocbi);
    CHECK(state.system_event_address == 0x9004u);

    state.pr = 0xDEAD6000u;
    run = execute_words({0x0183u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}, 0x8C026000u);
    CHECK(run);
    CHECK(state.last_system_event == jojo::Sh4ReferenceSystemEvent::pref);

    state.pr = 0xDEAD6000u;
    run = execute_words({0x0038u, 0x000Bu, 0x0009u}, state, {0x9000u, memory}, 0x8C026000u);
    CHECK(run);
    CHECK(state.last_system_event == jojo::Sh4ReferenceSystemEvent::ldtlb);

    run = execute_words({0x001Bu, 0x0009u}, state, {0x9000u, memory}, 0x8C026100u);
    CHECK(run);
    if (run) CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::sleep);
    CHECK(state.sleeping);
    CHECK(state.pc == 0x8C026102u);
}

int main() {
    test_irl13_enters_interrupt_vector_and_saves_state();
    test_imask_rejects_equal_or_lower_irq_level();
    test_bl_masks_irl_interrupts();
    test_invalid_irl_level_is_rejected();
    test_rte_restores_ssr_spc_t_and_register_bank_after_delay_slot();
    test_trapa_enters_general_vector_and_swaps_to_privileged_bank();
    test_ldc_stc_sr_and_bank_registers_switch_active_bank();
    test_tas_movca_cache_mmu_and_sleep_system_effects();
    if (failures) {
        std::cerr << failures << " SH-4 interrupt/system assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 interrupt/system assertions passed\n";
    return 0;
}
