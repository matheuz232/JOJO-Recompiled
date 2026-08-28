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
    add_reg,
    sub_reg,
    compare_eq,
    compare_eq_imm,
    compare_unsigned_ge,
    compare_signed_ge,
    compare_unsigned_gt,
    compare_signed_gt,
    compare_pz,
    compare_pl,
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
    shift_left_const,
    shift_right_logical_const,
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
