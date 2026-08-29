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
        case Sh4Op::clrt: out.op = Sh4IrOp::clear_t; break;
        case Sh4Op::sett: out.op = Sh4IrOp::set_t; break;
        case Sh4Op::movt: out.op = Sh4IrOp::move_t; break;
        case Sh4Op::mov_imm: out.op = Sh4IrOp::set_imm; break;
        case Sh4Op::add_imm: out.op = Sh4IrOp::add_imm; break;
        case Sh4Op::mov_reg: out.op = Sh4IrOp::copy_reg; break;
        case Sh4Op::movb_store: out.op = Sh4IrOp::store_mem8; break;
        case Sh4Op::movw_store: out.op = Sh4IrOp::store_mem16; break;
        case Sh4Op::movl_store: out.op = Sh4IrOp::store_mem32; break;
        case Sh4Op::movb_load: out.op = Sh4IrOp::load_mem8_signed; break;
        case Sh4Op::movw_load: out.op = Sh4IrOp::load_mem16_signed; break;
        case Sh4Op::movl_load: out.op = Sh4IrOp::load_mem32; break;
        case Sh4Op::movb_store_predec: out.op = Sh4IrOp::store_predec8; break;
        case Sh4Op::movw_store_predec: out.op = Sh4IrOp::store_predec16; break;
        case Sh4Op::movl_store_predec: out.op = Sh4IrOp::store_predec32; break;
        case Sh4Op::movb_load_postinc: out.op = Sh4IrOp::load_postinc8_signed; break;
        case Sh4Op::movw_load_postinc: out.op = Sh4IrOp::load_postinc16_signed; break;
        case Sh4Op::movl_load_postinc: out.op = Sh4IrOp::load_postinc32; break;
        case Sh4Op::movb_store_disp: out.op = Sh4IrOp::store_disp8; out.imm = input.displacement; break;
        case Sh4Op::movw_store_disp: out.op = Sh4IrOp::store_disp16; out.imm = input.displacement; break;
        case Sh4Op::movl_store_disp: out.op = Sh4IrOp::store_disp32; out.imm = input.displacement; break;
        case Sh4Op::movb_load_disp: out.op = Sh4IrOp::load_disp8_signed; out.imm = input.displacement; break;
        case Sh4Op::movw_load_disp: out.op = Sh4IrOp::load_disp16_signed; out.imm = input.displacement; break;
        case Sh4Op::movl_load_disp: out.op = Sh4IrOp::load_disp32; out.imm = input.displacement; break;
        case Sh4Op::movb_store_indexed: out.op = Sh4IrOp::store_indexed8; break;
        case Sh4Op::movw_store_indexed: out.op = Sh4IrOp::store_indexed16; break;
        case Sh4Op::movl_store_indexed: out.op = Sh4IrOp::store_indexed32; break;
        case Sh4Op::movb_load_indexed: out.op = Sh4IrOp::load_indexed8_signed; break;
        case Sh4Op::movw_load_indexed: out.op = Sh4IrOp::load_indexed16_signed; break;
        case Sh4Op::movl_load_indexed: out.op = Sh4IrOp::load_indexed32; break;
        case Sh4Op::movb_store_gbr_disp: out.op = Sh4IrOp::store_gbr_disp8; out.imm = input.displacement; break;
        case Sh4Op::movw_store_gbr_disp: out.op = Sh4IrOp::store_gbr_disp16; out.imm = input.displacement; break;
        case Sh4Op::movl_store_gbr_disp: out.op = Sh4IrOp::store_gbr_disp32; out.imm = input.displacement; break;
        case Sh4Op::movb_load_gbr_disp: out.op = Sh4IrOp::load_gbr_disp8_signed; out.imm = input.displacement; break;
        case Sh4Op::movw_load_gbr_disp: out.op = Sh4IrOp::load_gbr_disp16_signed; out.imm = input.displacement; break;
        case Sh4Op::movl_load_gbr_disp: out.op = Sh4IrOp::load_gbr_disp32; out.imm = input.displacement; break;
        case Sh4Op::ldc_gbr_reg: out.op = Sh4IrOp::set_gbr_from_reg; break;
        case Sh4Op::stc_gbr_reg: out.op = Sh4IrOp::copy_gbr_to_reg; break;
        case Sh4Op::ldc_gbr_postinc: out.op = Sh4IrOp::load_gbr_postinc32; break;
        case Sh4Op::stc_gbr_predec: out.op = Sh4IrOp::store_gbr_predec32; break;
        case Sh4Op::lds_mach_reg: out.op = Sh4IrOp::set_mach_from_reg; break;
        case Sh4Op::lds_macl_reg: out.op = Sh4IrOp::set_macl_from_reg; break;
        case Sh4Op::lds_pr_reg: out.op = Sh4IrOp::set_pr_from_reg; break;
        case Sh4Op::sts_mach_reg: out.op = Sh4IrOp::copy_mach_to_reg; break;
        case Sh4Op::sts_macl_reg: out.op = Sh4IrOp::copy_macl_to_reg; break;
        case Sh4Op::sts_pr_reg: out.op = Sh4IrOp::copy_pr_to_reg; break;
        case Sh4Op::lds_mach_postinc: out.op = Sh4IrOp::load_mach_postinc32; break;
        case Sh4Op::lds_macl_postinc: out.op = Sh4IrOp::load_macl_postinc32; break;
        case Sh4Op::lds_pr_postinc: out.op = Sh4IrOp::load_pr_postinc32; break;
        case Sh4Op::sts_mach_predec: out.op = Sh4IrOp::store_mach_predec32; break;
        case Sh4Op::sts_macl_predec: out.op = Sh4IrOp::store_macl_predec32; break;
        case Sh4Op::sts_pr_predec: out.op = Sh4IrOp::store_pr_predec32; break;
        case Sh4Op::lds_fpul_reg: out.op = Sh4IrOp::set_fpul_from_reg; break;
        case Sh4Op::sts_fpul_reg: out.op = Sh4IrOp::copy_fpul_to_reg; break;
        case Sh4Op::lds_fpul_postinc: out.op = Sh4IrOp::load_fpul_postinc32; break;
        case Sh4Op::sts_fpul_predec: out.op = Sh4IrOp::store_fpul_predec32; break;
        case Sh4Op::lds_fpscr_reg: out.op = Sh4IrOp::set_fpscr_from_reg; break;
        case Sh4Op::sts_fpscr_reg: out.op = Sh4IrOp::copy_fpscr_to_reg; break;
        case Sh4Op::lds_fpscr_postinc: out.op = Sh4IrOp::load_fpscr_postinc32; break;
        case Sh4Op::sts_fpscr_predec: out.op = Sh4IrOp::store_fpscr_predec32; break;
        case Sh4Op::clrmac: out.op = Sh4IrOp::clear_mac; break;
        case Sh4Op::mul_l: out.op = Sh4IrOp::multiply_low32; break;
        case Sh4Op::muls_w: out.op = Sh4IrOp::multiply_signed_word; break;
        case Sh4Op::mulu_w: out.op = Sh4IrOp::multiply_unsigned_word; break;
        case Sh4Op::dmuls_l: out.op = Sh4IrOp::multiply_signed_long; break;
        case Sh4Op::dmulu_l: out.op = Sh4IrOp::multiply_unsigned_long; break;
        case Sh4Op::exts_b: out.op = Sh4IrOp::sign_extend_byte; break;
        case Sh4Op::exts_w: out.op = Sh4IrOp::sign_extend_word; break;
        case Sh4Op::extu_b: out.op = Sh4IrOp::zero_extend_byte; break;
        case Sh4Op::extu_w: out.op = Sh4IrOp::zero_extend_word; break;
        case Sh4Op::swap_b: out.op = Sh4IrOp::swap_low_bytes; break;
        case Sh4Op::swap_w: out.op = Sh4IrOp::swap_words; break;
        case Sh4Op::xtrct: out.op = Sh4IrOp::extract_middle; break;
        case Sh4Op::add_reg: out.op = Sh4IrOp::add_reg; break;
        case Sh4Op::addc_reg: out.op = Sh4IrOp::add_with_carry; break;
        case Sh4Op::addv_reg: out.op = Sh4IrOp::add_with_overflow; break;
        case Sh4Op::sub_reg: out.op = Sh4IrOp::sub_reg; break;
        case Sh4Op::subc_reg: out.op = Sh4IrOp::sub_with_borrow; break;
        case Sh4Op::subv_reg: out.op = Sh4IrOp::sub_with_overflow; break;
        case Sh4Op::negc_reg: out.op = Sh4IrOp::negate_with_borrow; break;
        case Sh4Op::cmp_eq_reg: out.op = Sh4IrOp::compare_eq; break;
        case Sh4Op::cmp_eq_imm: out.op = Sh4IrOp::compare_eq_imm; break;
        case Sh4Op::cmp_hs_reg: out.op = Sh4IrOp::compare_unsigned_ge; break;
        case Sh4Op::cmp_ge_reg: out.op = Sh4IrOp::compare_signed_ge; break;
        case Sh4Op::cmp_hi_reg: out.op = Sh4IrOp::compare_unsigned_gt; break;
        case Sh4Op::cmp_gt_reg: out.op = Sh4IrOp::compare_signed_gt; break;
        case Sh4Op::cmp_pz: out.op = Sh4IrOp::compare_pz; break;
        case Sh4Op::cmp_pl: out.op = Sh4IrOp::compare_pl; break;
        case Sh4Op::tst_reg: out.op = Sh4IrOp::test_bits_reg; break;
        case Sh4Op::and_reg: out.op = Sh4IrOp::bit_and_reg; break;
        case Sh4Op::xor_reg: out.op = Sh4IrOp::bit_xor_reg; break;
        case Sh4Op::or_reg: out.op = Sh4IrOp::bit_or_reg; break;
        case Sh4Op::tst_imm: out.op = Sh4IrOp::test_bits_imm; break;
        case Sh4Op::and_imm: out.op = Sh4IrOp::bit_and_imm; break;
        case Sh4Op::xor_imm: out.op = Sh4IrOp::bit_xor_imm; break;
        case Sh4Op::or_imm: out.op = Sh4IrOp::bit_or_imm; break;
        case Sh4Op::not_reg: out.op = Sh4IrOp::bit_not; break;
        case Sh4Op::neg_reg: out.op = Sh4IrOp::negate; break;
        case Sh4Op::shll: out.op = Sh4IrOp::shift_left_one; break;
        case Sh4Op::shlr: out.op = Sh4IrOp::shift_right_logical_one; break;
        case Sh4Op::shar: out.op = Sh4IrOp::shift_right_arithmetic_one; break;
        case Sh4Op::shll2: out.op = Sh4IrOp::shift_left_const; out.imm = 2; break;
        case Sh4Op::shlr2: out.op = Sh4IrOp::shift_right_logical_const; out.imm = 2; break;
        case Sh4Op::shll8: out.op = Sh4IrOp::shift_left_const; out.imm = 8; break;
        case Sh4Op::shlr8: out.op = Sh4IrOp::shift_right_logical_const; out.imm = 8; break;
        case Sh4Op::shll16: out.op = Sh4IrOp::shift_left_const; out.imm = 16; break;
        case Sh4Op::shlr16: out.op = Sh4IrOp::shift_right_logical_const; out.imm = 16; break;
        case Sh4Op::fldi0: out.op = Sh4IrOp::set_fr_zero; break;
        case Sh4Op::fldi1: out.op = Sh4IrOp::set_fr_one; break;
        case Sh4Op::flds: out.op = Sh4IrOp::copy_fr_to_fpul; break;
        case Sh4Op::fsts: out.op = Sh4IrOp::copy_fpul_to_fr; break;
        case Sh4Op::float_fpul: out.op = Sh4IrOp::convert_fpul_to_float; break;
        case Sh4Op::ftrc: out.op = Sh4IrOp::truncate_float_to_fpul; break;
        case Sh4Op::fadd: out.op = Sh4IrOp::add_single_float; break;
        case Sh4Op::fsub: out.op = Sh4IrOp::subtract_single_float; break;
        case Sh4Op::fmul: out.op = Sh4IrOp::multiply_single_float; break;
        case Sh4Op::fdiv: out.op = Sh4IrOp::divide_single_float; break;
        case Sh4Op::fmov_reg: out.op = Sh4IrOp::copy_fpu_registers; break;
        case Sh4Op::fmov_store: out.op = Sh4IrOp::store_fpu_memory; break;
        case Sh4Op::fmov_load: out.op = Sh4IrOp::load_fpu_memory; break;
        case Sh4Op::fmov_load_postinc: out.op = Sh4IrOp::load_fpu_postincrement; break;
        case Sh4Op::fmov_store_predec: out.op = Sh4IrOp::store_fpu_predecrement; break;
        case Sh4Op::fmov_load_indexed: out.op = Sh4IrOp::load_fpu_indexed; break;
        case Sh4Op::fmov_store_indexed: out.op = Sh4IrOp::store_fpu_indexed; break;
        case Sh4Op::frchg: out.op = Sh4IrOp::toggle_fpscr_fr; break;
        case Sh4Op::fschg: out.op = Sh4IrOp::toggle_fpscr_sz; break;
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
