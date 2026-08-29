#pragma once
#include "core/result.h"
#include "core/sh4_cfg.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

enum class Sh4IrOp {
    nop,
    clear_t,
    set_t,
    move_t,
    set_imm,
    add_imm,
    copy_reg,
    store_mem8,
    store_mem16,
    store_mem32,
    load_mem8_signed,
    load_mem16_signed,
    load_mem32,
    store_predec8,
    store_predec16,
    store_predec32,
    load_postinc8_signed,
    load_postinc16_signed,
    load_postinc32,
    store_disp8,
    store_disp16,
    store_disp32,
    load_disp8_signed,
    load_disp16_signed,
    load_disp32,
    store_indexed8,
    store_indexed16,
    store_indexed32,
    load_indexed8_signed,
    load_indexed16_signed,
    load_indexed32,
    store_gbr_disp8,
    store_gbr_disp16,
    store_gbr_disp32,
    load_gbr_disp8_signed,
    load_gbr_disp16_signed,
    load_gbr_disp32,
    set_gbr_from_reg,
    copy_gbr_to_reg,
    load_gbr_postinc32,
    store_gbr_predec32,
    set_mach_from_reg,
    set_macl_from_reg,
    set_pr_from_reg,
    copy_mach_to_reg,
    copy_macl_to_reg,
    copy_pr_to_reg,
    load_mach_postinc32,
    load_macl_postinc32,
    load_pr_postinc32,
    store_mach_predec32,
    store_macl_predec32,
    store_pr_predec32,
    set_fpul_from_reg,
    copy_fpul_to_reg,
    load_fpul_postinc32,
    store_fpul_predec32,
    set_fpscr_from_reg,
    copy_fpscr_to_reg,
    load_fpscr_postinc32,
    store_fpscr_predec32,
    clear_mac,
    multiply_low32,
    multiply_signed_word,
    multiply_unsigned_word,
    multiply_signed_long,
    multiply_unsigned_long,
    sign_extend_byte,
    sign_extend_word,
    zero_extend_byte,
    zero_extend_word,
    swap_low_bytes,
    swap_words,
    extract_middle,
    add_reg,
    add_with_carry,
    add_with_overflow,
    sub_reg,
    sub_with_borrow,
    sub_with_overflow,
    negate_with_borrow,
    compare_eq,
    compare_eq_imm,
    compare_unsigned_ge,
    compare_signed_ge,
    compare_unsigned_gt,
    compare_signed_gt,
    compare_pz,
    compare_pl,
    decrement_and_test,
    test_bits_reg,
    bit_and_reg,
    bit_xor_reg,
    bit_or_reg,
    test_bits_imm,
    bit_and_imm,
    bit_xor_imm,
    bit_or_imm,
    bit_not,
    negate,
    shift_left_one,
    shift_right_logical_one,
    shift_right_arithmetic_one,
    rotate_left_one,
    rotate_right_one,
    rotate_left_through_t,
    rotate_right_through_t,
    shift_left_const,
    shift_right_logical_const,
    set_fr_zero,
    set_fr_one,
    copy_fr_to_fpul,
    copy_fpul_to_fr,
    convert_fpul_to_float,
    truncate_float_to_fpul,
    negate_single_float,
    absolute_single_float,
    sqrt_single_float,
    multiply_add_single_float,
    add_single_float,
    subtract_single_float,
    multiply_single_float,
    divide_single_float,
    compare_single_float_eq,
    compare_single_float_gt,
    copy_fpu_registers,
    store_fpu_memory,
    load_fpu_memory,
    load_fpu_postincrement,
    store_fpu_predecrement,
    load_fpu_indexed,
    store_fpu_indexed,
    toggle_fpscr_fr,
    toggle_fpscr_sz,
    branch_direct,
    branch_if_t,
    branch_if_not_t,
    call_direct,
    jump_reg,
    call_reg,
    return_pr,
    return_exception,
    load_pc_word,
    load_pc_long,
    load_pc_address,
};

enum class Sh4IrExit {
    end_of_stream,
    fallthrough,
    conditional_branch,
    direct_branch,
    direct_call,
    indirect_call,
    indirect_jump,
    return_subroutine,
    return_exception,
};

struct Sh4IrInstruction {
    Sh4IrOp op{Sh4IrOp::nop};
    std::uint32_t source_address{};
    std::uint8_t dst_reg{0xFF};
    std::uint8_t src_reg{0xFF};
    std::int32_t imm{};
    std::uint32_t target{};
    bool in_delay_slot{};
};

struct Sh4IrBlock {
    std::uint32_t start_address{};
    std::vector<Sh4IrInstruction> ops;
    Sh4IrExit exit{Sh4IrExit::end_of_stream};
    std::optional<std::uint32_t> branch_target;
    std::optional<std::uint32_t> fallthrough_target;
};

struct Sh4IrProgram {
    std::uint32_t entry_address{};
    std::vector<Sh4IrBlock> blocks;
};

[[nodiscard]] Result<Sh4IrProgram> lift_sh4_cfg(const Sh4ControlFlowGraph& cfg);

[[nodiscard]] const Sh4IrBlock* find_sh4_ir_block(
    const Sh4IrProgram& program,
    std::uint32_t start_address) noexcept;

}
