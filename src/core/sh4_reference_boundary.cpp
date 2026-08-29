#include "core/sh4_reference_executor.h"

namespace jojo {

Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceMemoryView memory,
    std::size_t max_blocks,
    const Sh4ReferenceBlockBoundaryHook& boundary_hook) {
    if (max_blocks == 0u) {
        return Result<Sh4ReferenceRunResult>::failure(
            ErrorCode::invalid_argument,
            "reference executor block limit must be non-zero");
    }
    if (!find_sh4_ir_block(program, program.entry_address)) {
        return Result<Sh4ReferenceRunResult>::failure(
            ErrorCode::invalid_argument,
            "reference executor entry block is missing");
    }

    Sh4ReferenceRunResult total{};
    state.pc = program.entry_address;

    while (total.blocks_executed < max_blocks) {
        if (boundary_hook) {
            auto boundary = boundary_hook(state);
            if (!boundary) {
                return Result<Sh4ReferenceRunResult>::failure(
                    boundary.error,
                    boundary.detail);
            }
        }

        if (!find_sh4_ir_block(program, state.pc)) {
            total.stop_reason = Sh4ReferenceStopReason::left_program;
            return Result<Sh4ReferenceRunResult>::success(total);
        }

        Sh4IrProgram one_block_program = program;
        one_block_program.entry_address = state.pc;
        auto step = execute_sh4_ir_reference(
            one_block_program,
            state,
            memory,
            1u);
        if (!step) {
            return Result<Sh4ReferenceRunResult>::failure(step.error, step.detail);
        }

        total.blocks_executed += step.value.blocks_executed;
        total.operations_executed += step.value.operations_executed;

        if (step.value.stop_reason != Sh4ReferenceStopReason::block_limit) {
            total.stop_reason = step.value.stop_reason;
            return Result<Sh4ReferenceRunResult>::success(total);
        }
    }

    total.stop_reason = Sh4ReferenceStopReason::block_limit;
    return Result<Sh4ReferenceRunResult>::success(total);
}

Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceBus& bus,
    std::size_t max_blocks,
    const Sh4ReferenceBlockBoundaryHook& boundary_hook) {
    try {
        return execute_sh4_ir_reference(
            program,
            state,
            Sh4ReferenceMemoryView::for_bus(bus),
            max_blocks,
            boundary_hook);
    } catch (const Sh4ReferenceBusFailure& failure) {
        return Result<Sh4ReferenceRunResult>::failure(failure.error, failure.detail);
    }
}

}
