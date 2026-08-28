#include "core/sh4_reference_executor.h"

#include <optional>

namespace jojo {
namespace {

Result<void> require_register(std::uint8_t reg) {
    if (reg >= 16) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "SH-4 IR register index is out of range");
    }
    return Result<void>::success();
}

Result<std::uint16_t> read_u16(Sh4ReferenceMemoryView memory,
                               std::uint32_t address) {
    if (address < memory.base_address) {
        return Result<std::uint16_t>::failure(ErrorCode::invalid_argument,
                                              "reference memory read is below the mapped range");
    }
    const auto offset = static_cast<std::size_t>(address - memory.base_address);
    if (offset > memory.bytes.size() || memory.bytes.size() - offset < 2) {
        return Result<std::uint16_t>::failure(ErrorCode::invalid_argument,
                                              "reference memory word read is outside the mapped range");
    }
    const auto value = static_cast<std::uint16_t>(memory.bytes[offset]) |
                       static_cast<std::uint16_t>(memory.bytes[offset + 1]) << 8u;
    return Result<std::uint16_t>::success(value);
}

Result<std::uint32_t> read_u32(Sh4ReferenceMemoryView memory,
                               std::uint32_t address) {
    if (address < memory.base_address) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_argument,
                                              "reference memory read is below the mapped range");
    }
    const auto offset = static_cast<std::size_t>(address - memory.base_address);
    if (offset > memory.bytes.size() || memory.bytes.size() - offset < 4) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_argument,
                                              "reference memory long read is outside the mapped range");
    }
    const auto value = static_cast<std::uint32_t>(memory.bytes[offset]) |
                       static_cast<std::uint32_t>(memory.bytes[offset + 1]) << 8u |
                       static_cast<std::uint32_t>(memory.bytes[offset + 2]) << 16u |
                       static_cast<std::uint32_t>(memory.bytes[offset + 3]) << 24u;
    return Result<std::uint32_t>::success(value);
}

struct PendingTransfer {
    std::uint32_t target{};
    std::optional<bool> condition;
};

Result<void> execute_op(const Sh4IrInstruction& instruction,
                        Sh4ReferenceState& state,
                        Sh4ReferenceMemoryView memory,
                        std::optional<PendingTransfer>& pending) {
    switch (instruction.op) {
        case Sh4IrOp::nop:
            return Result<void>::success();

        case Sh4IrOp::set_imm: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.r[instruction.dst_reg] = static_cast<std::uint32_t>(instruction.imm);
            return Result<void>::success();
        }

        case Sh4IrOp::add_imm: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.r[instruction.dst_reg] += static_cast<std::uint32_t>(instruction.imm);
            return Result<void>::success();
        }

        case Sh4IrOp::copy_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] = state.r[instruction.src_reg];
            return Result<void>::success();
        }

        case Sh4IrOp::add_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] += state.r[instruction.src_reg];
            return Result<void>::success();
        }

        case Sh4IrOp::sub_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] -= state.r[instruction.src_reg];
            return Result<void>::success();
        }

        case Sh4IrOp::compare_eq: {
            auto lhs = require_register(instruction.dst_reg);
            if (!lhs) return lhs;
            auto rhs = require_register(instruction.src_reg);
            if (!rhs) return rhs;
            state.t = state.r[instruction.dst_reg] == state.r[instruction.src_reg];
            return Result<void>::success();
        }

        case Sh4IrOp::branch_direct:
            pending = PendingTransfer{instruction.target, std::nullopt};
            return Result<void>::success();

        case Sh4IrOp::branch_if_t:
            pending = PendingTransfer{instruction.target, state.t};
            return Result<void>::success();

        case Sh4IrOp::branch_if_not_t:
            pending = PendingTransfer{instruction.target, !state.t};
            return Result<void>::success();

        case Sh4IrOp::call_direct:
            state.pr = instruction.source_address + 4u;
            pending = PendingTransfer{instruction.target, std::nullopt};
            return Result<void>::success();

        case Sh4IrOp::jump_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            pending = PendingTransfer{state.r[instruction.src_reg], std::nullopt};
            return Result<void>::success();
        }

        case Sh4IrOp::call_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto target = state.r[instruction.src_reg];
            state.pr = instruction.source_address + 4u;
            pending = PendingTransfer{target, std::nullopt};
            return Result<void>::success();
        }

        case Sh4IrOp::return_pr:
            pending = PendingTransfer{state.pr, std::nullopt};
            return Result<void>::success();

        case Sh4IrOp::return_exception:
            return Result<void>::failure(
                ErrorCode::unsupported_format,
                "reference executor does not model SPC/SSR for RTE yet");

        case Sh4IrOp::load_pc_word: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto value = read_u16(memory, instruction.target);
            if (!value) return Result<void>::failure(value.error, value.detail);
            const auto signed_value = static_cast<std::int16_t>(value.value);
            state.r[instruction.dst_reg] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(signed_value));
            return Result<void>::success();
        }

        case Sh4IrOp::load_pc_long: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto value = read_u32(memory, instruction.target);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }

        case Sh4IrOp::load_pc_address: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.r[instruction.dst_reg] = instruction.target;
            return Result<void>::success();
        }
    }
    return Result<void>::failure(ErrorCode::unsupported_format,
                                 "reference executor encountered an unknown SH-4 IR operation");
}

Result<std::uint32_t> resolve_exit(const Sh4IrBlock& block,
                                   const std::optional<PendingTransfer>& pending,
                                   Sh4ReferenceStopReason& stop_reason) {
    switch (block.exit) {
        case Sh4IrExit::end_of_stream: {
            stop_reason = Sh4ReferenceStopReason::end_of_stream;
            if (block.ops.empty()) return Result<std::uint32_t>::success(block.start_address);
            return Result<std::uint32_t>::success(block.ops.back().source_address + 2u);
        }

        case Sh4IrExit::fallthrough:
            if (!block.fallthrough_target) {
                return Result<std::uint32_t>::failure(ErrorCode::invalid_argument,
                                                      "IR fallthrough block is missing its target");
            }
            return Result<std::uint32_t>::success(*block.fallthrough_target);

        case Sh4IrExit::conditional_branch:
            if (!pending || !pending->condition || !block.fallthrough_target) {
                return Result<std::uint32_t>::failure(
                    ErrorCode::invalid_argument,
                    "IR conditional block is missing a latched branch decision or fallthrough");
            }
            if (block.branch_target && pending->target != *block.branch_target) {
                return Result<std::uint32_t>::failure(ErrorCode::invalid_argument,
                                                      "latched conditional target disagrees with CFG metadata");
            }
            return Result<std::uint32_t>::success(*pending->condition
                                                      ? pending->target
                                                      : *block.fallthrough_target);

        case Sh4IrExit::direct_branch:
        case Sh4IrExit::direct_call:
        case Sh4IrExit::indirect_call:
        case Sh4IrExit::indirect_jump:
        case Sh4IrExit::return_subroutine:
            if (!pending) {
                return Result<std::uint32_t>::failure(ErrorCode::invalid_argument,
                                                      "IR control-flow block did not latch a target");
            }
            if ((block.exit == Sh4IrExit::direct_branch || block.exit == Sh4IrExit::direct_call) &&
                block.branch_target && pending->target != *block.branch_target) {
                return Result<std::uint32_t>::failure(ErrorCode::invalid_argument,
                                                      "latched direct target disagrees with CFG metadata");
            }
            return Result<std::uint32_t>::success(pending->target);

        case Sh4IrExit::return_exception:
            return Result<std::uint32_t>::failure(
                ErrorCode::unsupported_format,
                "reference executor does not model exception return state yet");
    }
    return Result<std::uint32_t>::failure(ErrorCode::unsupported_format,
                                          "reference executor encountered an unknown IR block exit");
}

}

Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceMemoryView memory,
    std::size_t max_blocks) {
    if (max_blocks == 0) {
        return Result<Sh4ReferenceRunResult>::failure(ErrorCode::invalid_argument,
                                                      "reference executor block limit must be non-zero");
    }
    if (!find_sh4_ir_block(program, program.entry_address)) {
        return Result<Sh4ReferenceRunResult>::failure(ErrorCode::invalid_argument,
                                                      "reference executor entry block is missing");
    }

    Sh4ReferenceRunResult run{};
    state.pc = program.entry_address;

    while (run.blocks_executed < max_blocks) {
        const auto* block = find_sh4_ir_block(program, state.pc);
        if (!block) {
            run.stop_reason = Sh4ReferenceStopReason::left_program;
            return Result<Sh4ReferenceRunResult>::success(run);
        }

        std::optional<PendingTransfer> pending;
        for (const auto& instruction : block->ops) {
            auto executed = execute_op(instruction, state, memory, pending);
            if (!executed) {
                return Result<Sh4ReferenceRunResult>::failure(executed.error, executed.detail);
            }
            ++run.operations_executed;
        }
        ++run.blocks_executed;

        Sh4ReferenceStopReason exit_reason{Sh4ReferenceStopReason::left_program};
        auto next_pc = resolve_exit(*block, pending, exit_reason);
        if (!next_pc) {
            return Result<Sh4ReferenceRunResult>::failure(next_pc.error, next_pc.detail);
        }
        state.pc = next_pc.value;

        if (block->exit == Sh4IrExit::end_of_stream) {
            run.stop_reason = exit_reason;
            return Result<Sh4ReferenceRunResult>::success(run);
        }

        if (!find_sh4_ir_block(program, state.pc)) {
            run.stop_reason = Sh4ReferenceStopReason::left_program;
            return Result<Sh4ReferenceRunResult>::success(run);
        }
    }

    run.stop_reason = Sh4ReferenceStopReason::block_limit;
    return Result<Sh4ReferenceRunResult>::success(run);
}

}
