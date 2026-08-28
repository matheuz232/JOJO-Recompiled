#include "core/sh4_ir.h"
#include <algorithm>

namespace jojo {
namespace {

Result<Sh4IrInstruction> lift_instruction(const Sh4Instruction& input,
                                          bool in_delay_slot) {
    Sh4IrInstruction out{};
    out.source_address = input.address;
    out.in_delay_slot = in_delay_slot;
    out.dst_reg = input.rn;
    out.src_reg = input.rm;
    out.imm = input.immediate;

    switch (input.op) {
        case Sh4Op::nop: out.op = Sh4IrOp::nop; break;
        case Sh4Op::mov_imm: out.op = Sh4IrOp::set_imm; break;
        case Sh4Op::add_imm: out.op = Sh4IrOp::add_imm; break;
        case Sh4Op::mov_reg: out.op = Sh4IrOp::copy_reg; break;
        case Sh4Op::add_reg: out.op = Sh4IrOp::add_reg; break;
        case Sh4Op::sub_reg: out.op = Sh4IrOp::sub_reg; break;
        case Sh4Op::cmp_eq_reg: out.op = Sh4IrOp::compare_eq; break;
        case Sh4Op::bra:
            out.op = Sh4IrOp::branch_direct;
            if (const auto target = sh4_direct_target(input)) out.target = *target;
            break;
        case Sh4Op::bt:
        case Sh4Op::bt_s:
            out.op = Sh4IrOp::branch_if_t;
            if (const auto target = sh4_direct_target(input)) out.target = *target;
            break;
        case Sh4Op::bf:
        case Sh4Op::bf_s:
            out.op = Sh4IrOp::branch_if_not_t;
            if (const auto target = sh4_direct_target(input)) out.target = *target;
            break;
        case Sh4Op::bsr:
            out.op = Sh4IrOp::call_direct;
            if (const auto target = sh4_direct_target(input)) out.target = *target;
            break;
        case Sh4Op::jmp_reg:
            out.op = Sh4IrOp::jump_reg;
            out.src_reg = input.rn;
            break;
        case Sh4Op::jsr_reg:
            out.op = Sh4IrOp::call_reg;
            out.src_reg = input.rn;
            break;
        case Sh4Op::rts: out.op = Sh4IrOp::return_pr; break;
        case Sh4Op::rte: out.op = Sh4IrOp::return_exception; break;
        case Sh4Op::movw_pc:
            out.op = Sh4IrOp::load_pc_word;
            if (const auto target = sh4_pc_relative_address(input)) out.target = *target;
            break;
        case Sh4Op::movl_pc:
            out.op = Sh4IrOp::load_pc_long;
            if (const auto target = sh4_pc_relative_address(input)) out.target = *target;
            break;
        case Sh4Op::mova_pc:
            out.op = Sh4IrOp::load_pc_address;
            if (const auto target = sh4_pc_relative_address(input)) out.target = *target;
            break;
        case Sh4Op::unsupported:
            return Result<Sh4IrInstruction>::failure(
                ErrorCode::unsupported_format,
                "cannot lift an unsupported SH-4 opcode into IR");
    }
    return Result<Sh4IrInstruction>::success(out);
}

Result<Sh4IrExit> lift_exit(Sh4BlockExit exit) {
    switch (exit) {
        case Sh4BlockExit::end_of_stream: return Result<Sh4IrExit>::success(Sh4IrExit::end_of_stream);
        case Sh4BlockExit::fallthrough: return Result<Sh4IrExit>::success(Sh4IrExit::fallthrough);
        case Sh4BlockExit::conditional_branch: return Result<Sh4IrExit>::success(Sh4IrExit::conditional_branch);
        case Sh4BlockExit::direct_branch: return Result<Sh4IrExit>::success(Sh4IrExit::direct_branch);
        case Sh4BlockExit::direct_call: return Result<Sh4IrExit>::success(Sh4IrExit::direct_call);
        case Sh4BlockExit::indirect_call: return Result<Sh4IrExit>::success(Sh4IrExit::indirect_call);
        case Sh4BlockExit::indirect_jump: return Result<Sh4IrExit>::success(Sh4IrExit::indirect_jump);
        case Sh4BlockExit::return_subroutine: return Result<Sh4IrExit>::success(Sh4IrExit::return_subroutine);
        case Sh4BlockExit::return_exception: return Result<Sh4IrExit>::success(Sh4IrExit::return_exception);
        case Sh4BlockExit::unsupported_instruction:
            return Result<Sh4IrExit>::failure(
                ErrorCode::unsupported_format,
                "cannot lift a CFG block terminated by an unsupported SH-4 instruction");
    }
    return Result<Sh4IrExit>::failure(ErrorCode::unsupported_format, "unknown SH-4 CFG exit");
}

}

Result<Sh4IrProgram> lift_sh4_cfg(const Sh4ControlFlowGraph& cfg) {
    Sh4IrProgram program{};
    program.entry_address = cfg.entry_address;
    program.blocks.reserve(cfg.blocks.size());

    for (const auto& block : cfg.blocks) {
        auto lifted_exit = lift_exit(block.exit);
        if (!lifted_exit) return Result<Sh4IrProgram>::failure(lifted_exit.error, lifted_exit.detail);

        Sh4IrBlock ir_block{};
        ir_block.start_address = block.start_address;
        ir_block.exit = lifted_exit.value;
        ir_block.branch_target = block.branch_target;
        ir_block.fallthrough_target = block.fallthrough_target;
        ir_block.ops.reserve(block.instructions.size());

        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const bool in_delay_slot = index > 0 && block.instructions[index - 1].has_delay_slot;
            auto op = lift_instruction(block.instructions[index], in_delay_slot);
            if (!op) return Result<Sh4IrProgram>::failure(op.error, op.detail);
            ir_block.ops.push_back(std::move(op.value));
        }
        program.blocks.push_back(std::move(ir_block));
    }

    std::sort(program.blocks.begin(), program.blocks.end(), [](const Sh4IrBlock& a, const Sh4IrBlock& b) {
        return a.start_address < b.start_address;
    });
    return Result<Sh4IrProgram>::success(std::move(program));
}

const Sh4IrBlock* find_sh4_ir_block(const Sh4IrProgram& program,
                                    std::uint32_t start_address) noexcept {
    const auto it = std::lower_bound(program.blocks.begin(), program.blocks.end(), start_address,
                                     [](const Sh4IrBlock& block, std::uint32_t address) {
                                         return block.start_address < address;
                                     });
    if (it == program.blocks.end() || it->start_address != start_address) return nullptr;
    return &*it;
}

}
