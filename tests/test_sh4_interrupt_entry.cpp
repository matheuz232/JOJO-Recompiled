#include "core/sh4_reference_executor.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void test_irl13_enters_interrupt_vector_and_saves_state() {
    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010100u;
    state.r[15] = 0x8CFF0000u;
    state.vbr = 0x8C000000u;
    state.sr = 0x00000000u;

    const auto accepted = jojo::accept_sh4_irl_interrupt(state, 13u);
    CHECK(accepted);
    if (!accepted) return;
    CHECK(accepted.value);
    CHECK(state.spc == 0x8C010100u);
    CHECK(state.ssr == 0x00000000u);
    CHECK(state.sgr == 0x8CFF0000u);
    CHECK(state.intevt == 0x00000240u);
    CHECK(state.pc == 0x8C000600u);
    CHECK((state.sr & 0x70000000u) == 0x70000000u); // MD | RB | BL
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

static void test_rte_restores_ssr_and_spc_after_delay_slot() {
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
    state.ssr = 0x00000020u;
    state.spc = 0x8C010100u;

    jojo::Sh4ReferenceMemoryView memory{};
    const auto run = jojo::execute_sh4_ir_reference(program, state, memory, 4u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.operations_executed == 2u);
    CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::left_program);
    CHECK(state.sr == 0x00000020u);
    CHECK(state.pc == 0x8C010100u);
}

int main() {
    test_irl13_enters_interrupt_vector_and_saves_state();
    test_imask_rejects_equal_or_lower_irq_level();
    test_bl_masks_irl_interrupts();
    test_invalid_irl_level_is_rejected();
    test_rte_restores_ssr_and_spc_after_delay_slot();
    if (failures) {
        std::cerr << failures << " SH-4 interrupt-entry assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 interrupt-entry assertions passed\n";
    return 0;
}
