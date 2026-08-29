#include "core/sh4_reference_executor.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::Sh4IrInstruction set_imm(std::uint32_t address, std::uint8_t reg, std::int32_t value) {
    jojo::Sh4IrInstruction op{};
    op.op = jojo::Sh4IrOp::set_imm;
    op.source_address = address;
    op.dst_reg = reg;
    op.imm = value;
    return op;
}

static void test_boundary_hook_can_redirect_execution_to_interrupt_vector() {
    jojo::Sh4IrProgram program{};
    program.entry_address = 0x1000u;

    jojo::Sh4IrBlock first{};
    first.start_address = 0x1000u;
    first.ops.push_back({jojo::Sh4IrOp::nop, 0x1000u});
    first.exit = jojo::Sh4IrExit::fallthrough;
    first.fallthrough_target = 0x1002u;
    program.blocks.push_back(first);

    jojo::Sh4IrBlock interrupted{};
    interrupted.start_address = 0x1002u;
    interrupted.ops.push_back(set_imm(0x1002u, 0u, 1));
    interrupted.exit = jojo::Sh4IrExit::end_of_stream;
    program.blocks.push_back(interrupted);

    jojo::Sh4IrBlock handler{};
    handler.start_address = 0x1600u;
    handler.ops.push_back(set_imm(0x1600u, 1u, 2));
    handler.exit = jojo::Sh4IrExit::end_of_stream;
    program.blocks.push_back(handler);

    std::vector<std::uint8_t> bytes(16u, 0u);
    jojo::Sh4ReferenceState state{};
    state.vbr = 0x1000u;
    int boundaries = 0;

    jojo::Sh4ReferenceBlockBoundaryHook hook = [&](jojo::Sh4ReferenceState& current) {
        ++boundaries;
        if (boundaries == 2) {
            auto accepted = jojo::accept_sh4_irl_interrupt(current, 13u);
            CHECK(accepted);
            if (!accepted) return jojo::Result<void>::failure(accepted.error, accepted.detail);
            CHECK(accepted.value);
        }
        return jojo::Result<void>::success();
    };

    const auto run = jojo::execute_sh4_ir_reference(
        program,
        state,
        jojo::Sh4ReferenceMemoryView{0u, bytes},
        8u,
        hook);
    CHECK(run);
    if (!run) return;

    CHECK(boundaries == 2);
    CHECK(run.value.blocks_executed == 2u);
    CHECK(state.spc == 0x1002u);
    CHECK(state.pc == 0x1602u);
    CHECK(state.r[0] == 0u);
    CHECK(state.r[1] == 2u);
}

int main() {
    test_boundary_hook_can_redirect_execution_to_interrupt_vector();
    if (failures) {
        std::cerr << failures << " SH-4 block-boundary assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 block-boundary assertions passed\n";
    return 0;
}
