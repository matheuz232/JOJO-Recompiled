#include "core/sh4_reference_executor.h"

#include <bit>
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

std::int32_t as_signed(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

Result<std::size_t> memory_offset(Sh4ReferenceMemoryView memory,
                                  std::uint32_t address,
                                  std::size_t width,
                                  std::uint32_t alignment) {
    if (alignment > 1u && (address % alignment) != 0u) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "reference memory access is misaligned");
    }
    if (address < memory.base_address) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "reference memory access is below the mapped range");
    }
    const auto offset = static_cast<std::size_t>(address - memory.base_address);
    if (offset > memory.bytes.size() || width > memory.bytes.size() - offset) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "reference memory access is outside the mapped range");
    }
    return Result<std::size_t>::success(offset);
}

Result<std::uint8_t> read_u8(Sh4ReferenceMemoryView memory, std::uint32_t address) {
    auto offset = memory_offset(memory, address, 1, 1);
    if (!offset) return Result<std::uint8_t>::failure(offset.error, offset.detail);
    return Result<std::uint8_t>::success(memory.bytes[offset.value]);
}

Result<std::uint16_t> read_u16(Sh4ReferenceMemoryView memory, std::uint32_t address) {
    auto offset = memory_offset(memory, address, 2, 2);
    if (!offset) return Result<std::uint16_t>::failure(offset.error, offset.detail);
    const auto value = static_cast<std::uint16_t>(memory.bytes[offset.value]) |
                       static_cast<std::uint16_t>(memory.bytes[offset.value + 1]) << 8u;
    return Result<std::uint16_t>::success(value);
}

Result<std::uint32_t> read_u32(Sh4ReferenceMemoryView memory, std::uint32_t address) {
    auto offset = memory_offset(memory, address, 4, 4);
    if (!offset) return Result<std::uint32_t>::failure(offset.error, offset.detail);
    const auto value = static_cast<std::uint32_t>(memory.bytes[offset.value]) |
                       static_cast<std::uint32_t>(memory.bytes[offset.value + 1]) << 8u |
                       static_cast<std::uint32_t>(memory.bytes[offset.value + 2]) << 16u |
                       static_cast<std::uint32_t>(memory.bytes[offset.value + 3]) << 24u;
    return Result<std::uint32_t>::success(value);
}

Result<void> write_u8(Sh4ReferenceMemoryView memory,
                      std::uint32_t address,
                      std::uint32_t value) {
    auto offset = memory_offset(memory, address, 1, 1);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    memory.bytes[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    return Result<void>::success();
}

Result<void> write_u16(Sh4ReferenceMemoryView memory,
                       std::uint32_t address,
                       std::uint32_t value) {
    auto offset = memory_offset(memory, address, 2, 2);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    memory.bytes[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    memory.bytes[offset.value + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    return Result<void>::success();
}

Result<void> write_u32(Sh4ReferenceMemoryView memory,
                       std::uint32_t address,
                       std::uint32_t value) {
    auto offset = memory_offset(memory, address, 4, 4);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    memory.bytes[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    memory.bytes[offset.value + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    memory.bytes[offset.value + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    memory.bytes[offset.value + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    return Result<void>::success();
}

bool is_load8(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::load_mem8_signed || op == Sh4IrOp::load_postinc8_signed ||
           op == Sh4IrOp::load_disp8_signed || op == Sh4IrOp::load_indexed8_signed ||
           op == Sh4IrOp::load_gbr_disp8_signed;
}

bool is_load16(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::load_mem16_signed || op == Sh4IrOp::load_postinc16_signed ||
           op == Sh4IrOp::load_disp16_signed || op == Sh4IrOp::load_indexed16_signed ||
           op == Sh4IrOp::load_gbr_disp16_signed;
}

Result<std::uint32_t> load_memory_value(Sh4IrOp op,
                                        Sh4ReferenceMemoryView memory,
                                        std::uint32_t address) {
    if (is_load8(op)) {
        auto value = read_u8(memory, address);
        if (!value) return Result<std::uint32_t>::failure(value.error, value.detail);
        const auto signed_value = std::bit_cast<std::int8_t>(value.value);
        return Result<std::uint32_t>::success(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(signed_value)));
    }
    if (is_load16(op)) {
        auto value = read_u16(memory, address);
        if (!value) return Result<std::uint32_t>::failure(value.error, value.detail);
        const auto signed_value = std::bit_cast<std::int16_t>(value.value);
        return Result<std::uint32_t>::success(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(signed_value)));
    }
    auto value = read_u32(memory, address);
    if (!value) return Result<std::uint32_t>::failure(value.error, value.detail);
    return value;
}

bool is_store8(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::store_mem8 || op == Sh4IrOp::store_predec8 ||
           op == Sh4IrOp::store_disp8 || op == Sh4IrOp::store_indexed8 ||
           op == Sh4IrOp::store_gbr_disp8;
}

bool is_store16(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::store_mem16 || op == Sh4IrOp::store_predec16 ||
           op == Sh4IrOp::store_disp16 || op == Sh4IrOp::store_indexed16 ||
           op == Sh4IrOp::store_gbr_disp16;
}

Result<void> store_memory_value(Sh4IrOp op,
                                Sh4ReferenceMemoryView memory,
                                std::uint32_t address,
                                std::uint32_t value) {
    if (is_store8(op)) return write_u8(memory, address, value);
    if (is_store16(op)) return write_u16(memory, address, value);
    return write_u32(memory, address, value);
}

std::uint32_t memory_width(Sh4IrOp op) noexcept {
    if (is_store8(op) || is_load8(op)) return 1u;
    if (is_store16(op) || is_load16(op)) return 2u;
    return 4u;
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
        case Sh4IrOp::clear_t:
            state.t = false;
            return Result<void>::success();
        case Sh4IrOp::set_t:
            state.t = true;
            return Result<void>::success();
        case Sh4IrOp::move_t: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.r[instruction.dst_reg] = state.t ? 1u : 0u;
            return Result<void>::success();
        }
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

        case Sh4IrOp::store_mem8:
        case Sh4IrOp::store_mem16:
        case Sh4IrOp::store_mem32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            return store_memory_value(instruction.op, memory,
                                      state.r[instruction.dst_reg],
                                      state.r[instruction.src_reg]);
        }
        case Sh4IrOp::load_mem8_signed:
        case Sh4IrOp::load_mem16_signed:
        case Sh4IrOp::load_mem32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg];
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_predec8:
        case Sh4IrOp::store_predec16:
        case Sh4IrOp::store_predec32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            const auto source_value = state.r[instruction.src_reg];
            const auto address = state.r[instruction.dst_reg] - memory_width(instruction.op);
            auto stored = store_memory_value(instruction.op, memory, address, source_value);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::load_postinc8_signed:
        case Sh4IrOp::load_postinc16_signed:
        case Sh4IrOp::load_postinc32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg];
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            if (instruction.dst_reg != instruction.src_reg) {
                state.r[instruction.src_reg] += memory_width(instruction.op);
            }
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_disp8:
        case Sh4IrOp::store_disp16:
        case Sh4IrOp::store_disp32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            const auto address = state.r[instruction.dst_reg] + static_cast<std::uint32_t>(instruction.imm);
            return store_memory_value(instruction.op, memory, address, state.r[instruction.src_reg]);
        }
        case Sh4IrOp::load_disp8_signed:
        case Sh4IrOp::load_disp16_signed:
        case Sh4IrOp::load_disp32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg] + static_cast<std::uint32_t>(instruction.imm);
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_indexed8:
        case Sh4IrOp::store_indexed16:
        case Sh4IrOp::store_indexed32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            const auto address = state.r[0] + state.r[instruction.dst_reg];
            return store_memory_value(instruction.op, memory, address, state.r[instruction.src_reg]);
        }
        case Sh4IrOp::load_indexed8_signed:
        case Sh4IrOp::load_indexed16_signed:
        case Sh4IrOp::load_indexed32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[0] + state.r[instruction.src_reg];
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_gbr_disp8:
        case Sh4IrOp::store_gbr_disp16:
        case Sh4IrOp::store_gbr_disp32: {
            const auto address = state.gbr + static_cast<std::uint32_t>(instruction.imm);
            return store_memory_value(instruction.op, memory, address, state.r[0]);
        }
        case Sh4IrOp::load_gbr_disp8_signed:
        case Sh4IrOp::load_gbr_disp16_signed:
        case Sh4IrOp::load_gbr_disp32: {
            const auto address = state.gbr + static_cast<std::uint32_t>(instruction.imm);
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[0] = value.value;
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
        case Sh4IrOp::compare_eq_imm: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.t = state.r[instruction.dst_reg] == static_cast<std::uint32_t>(instruction.imm);
            return Result<void>::success();
        }
        case Sh4IrOp::compare_unsigned_ge:
        case Sh4IrOp::compare_signed_ge:
        case Sh4IrOp::compare_unsigned_gt:
        case Sh4IrOp::compare_signed_gt: {
            auto lhs = require_register(instruction.dst_reg);
            if (!lhs) return lhs;
            auto rhs = require_register(instruction.src_reg);
            if (!rhs) return rhs;
            const auto left = state.r[instruction.dst_reg];
            const auto right = state.r[instruction.src_reg];
            if (instruction.op == Sh4IrOp::compare_unsigned_ge) state.t = left >= right;
            if (instruction.op == Sh4IrOp::compare_signed_ge) state.t = as_signed(left) >= as_signed(right);
            if (instruction.op == Sh4IrOp::compare_unsigned_gt) state.t = left > right;
            if (instruction.op == Sh4IrOp::compare_signed_gt) state.t = as_signed(left) > as_signed(right);
            return Result<void>::success();
        }
        case Sh4IrOp::compare_pz:
        case Sh4IrOp::compare_pl: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            const auto value = as_signed(state.r[instruction.dst_reg]);
            state.t = instruction.op == Sh4IrOp::compare_pz ? value >= 0 : value > 0;
            return Result<void>::success();
        }
        case Sh4IrOp::test_bits_reg:
        case Sh4IrOp::bit_and_reg:
        case Sh4IrOp::bit_xor_reg:
        case Sh4IrOp::bit_or_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (instruction.op == Sh4IrOp::test_bits_reg) state.t = (state.r[instruction.dst_reg] & state.r[instruction.src_reg]) == 0u;
            else if (instruction.op == Sh4IrOp::bit_and_reg) state.r[instruction.dst_reg] &= state.r[instruction.src_reg];
            else if (instruction.op == Sh4IrOp::bit_xor_reg) state.r[instruction.dst_reg] ^= state.r[instruction.src_reg];
            else state.r[instruction.dst_reg] |= state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::test_bits_imm:
        case Sh4IrOp::bit_and_imm:
        case Sh4IrOp::bit_xor_imm:
        case Sh4IrOp::bit_or_imm: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto value = static_cast<std::uint32_t>(instruction.imm);
            if (instruction.op == Sh4IrOp::test_bits_imm) state.t = (state.r[instruction.dst_reg] & value) == 0u;
            else if (instruction.op == Sh4IrOp::bit_and_imm) state.r[instruction.dst_reg] &= value;
            else if (instruction.op == Sh4IrOp::bit_xor_imm) state.r[instruction.dst_reg] ^= value;
            else state.r[instruction.dst_reg] |= value;
            return Result<void>::success();
        }
        case Sh4IrOp::bit_not:
        case Sh4IrOp::negate: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] = instruction.op == Sh4IrOp::bit_not
                ? ~state.r[instruction.src_reg]
                : 0u - state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::shift_left_one:
        case Sh4IrOp::shift_right_logical_one:
        case Sh4IrOp::shift_right_arithmetic_one: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            const auto value = state.r[instruction.dst_reg];
            if (instruction.op == Sh4IrOp::shift_left_one) {
                state.t = (value & 0x80000000u) != 0u;
                state.r[instruction.dst_reg] = value << 1u;
            } else if (instruction.op == Sh4IrOp::shift_right_logical_one) {
                state.t = (value & 1u) != 0u;
                state.r[instruction.dst_reg] = value >> 1u;
            } else {
                state.t = (value & 1u) != 0u;
                state.r[instruction.dst_reg] = (value >> 1u) | (value & 0x80000000u);
            }
            return Result<void>::success();
        }
        case Sh4IrOp::shift_left_const:
        case Sh4IrOp::shift_right_logical_const: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            if (instruction.imm != 2 && instruction.imm != 8 && instruction.imm != 16) {
                return Result<void>::failure(ErrorCode::invalid_argument,
                                             "SH-4 constant shift count is unsupported");
            }
            const auto count = static_cast<unsigned>(instruction.imm);
            if (instruction.op == Sh4IrOp::shift_left_const) state.r[instruction.dst_reg] <<= count;
            else state.r[instruction.dst_reg] >>= count;
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
            return Result<void>::failure(ErrorCode::unsupported_format,
                                         "reference executor does not model SPC/SSR for RTE yet");
        case Sh4IrOp::load_pc_word: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto value = read_u16(memory, instruction.target);
            if (!value) return Result<void>::failure(value.error, value.detail);
            const auto signed_value = std::bit_cast<std::int16_t>(value.value);
            state.r[instruction.dst_reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(signed_value));
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
        case Sh4IrExit::end_of_stream:
            stop_reason = Sh4ReferenceStopReason::end_of_stream;
            return Result<std::uint32_t>::success(block.ops.empty()
                ? block.start_address
                : block.ops.back().source_address + 2u);
        case Sh4IrExit::fallthrough:
            if (!block.fallthrough_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "IR fallthrough block is missing its target");
            return Result<std::uint32_t>::success(*block.fallthrough_target);
        case Sh4IrExit::conditional_branch:
            if (!pending || !pending->condition || !block.fallthrough_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "IR conditional block is missing a latched branch decision or fallthrough");
            if (block.branch_target && pending->target != *block.branch_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "latched conditional target disagrees with CFG metadata");
            return Result<std::uint32_t>::success(*pending->condition ? pending->target : *block.fallthrough_target);
        case Sh4IrExit::direct_branch:
        case Sh4IrExit::direct_call:
        case Sh4IrExit::indirect_call:
        case Sh4IrExit::indirect_jump:
        case Sh4IrExit::return_subroutine:
            if (!pending) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "IR control-flow block did not latch a target");
            if ((block.exit == Sh4IrExit::direct_branch || block.exit == Sh4IrExit::direct_call) && block.branch_target && pending->target != *block.branch_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "latched direct target disagrees with CFG metadata");
            return Result<std::uint32_t>::success(pending->target);
        case Sh4IrExit::return_exception:
            return Result<std::uint32_t>::failure(ErrorCode::unsupported_format, "reference executor does not model exception return state yet");
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
    if (max_blocks == 0) return Result<Sh4ReferenceRunResult>::failure(ErrorCode::invalid_argument, "reference executor block limit must be non-zero");
    if (!find_sh4_ir_block(program, program.entry_address)) return Result<Sh4ReferenceRunResult>::failure(ErrorCode::invalid_argument, "reference executor entry block is missing");

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
            if (!executed) return Result<Sh4ReferenceRunResult>::failure(executed.error, executed.detail);
            ++run.operations_executed;
        }
        ++run.blocks_executed;
        Sh4ReferenceStopReason exit_reason{Sh4ReferenceStopReason::left_program};
        auto next_pc = resolve_exit(*block, pending, exit_reason);
        if (!next_pc) return Result<Sh4ReferenceRunResult>::failure(next_pc.error, next_pc.detail);
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
