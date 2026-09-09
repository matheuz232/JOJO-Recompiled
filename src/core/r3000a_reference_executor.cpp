#include "core/r3000a_reference_executor.h"

#include "core/mips_decoder.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace jojo {
namespace {

constexpr std::uint32_t kCauseExcCodeMask = 0x0000007cu;
constexpr std::uint32_t kCauseContextMask = 0xF0000000u; // BD, BT, CE
constexpr std::uint32_t kStatusModeStackMask = 0x0000003fu;
constexpr std::uint32_t kStatusBev = 1u << 22;

std::uint32_t sign_extend16(std::uint16_t value) noexcept {
    return (value & 0x8000u) != 0u ? (0xffff0000u | static_cast<std::uint32_t>(value))
                                  : static_cast<std::uint32_t>(value);
}

std::int32_t signed_view(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

std::uint32_t arithmetic_shift_right(std::uint32_t value, std::uint32_t amount) noexcept {
    amount &= 31u;
    if (amount == 0u) return value;
    const auto shifted = value >> amount;
    if ((value & 0x80000000u) == 0u) return shifted;
    return shifted | (0xffffffffu << (32u - amount));
}

bool add_overflow(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t result) noexcept {
    return ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) != 0u;
}

bool sub_overflow(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t result) noexcept {
    return (((lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) != 0u;
}

void write_gpr(R3000aState& state, std::uint8_t reg, std::uint32_t value) noexcept {
    if (reg != 0u) state.gpr[reg] = value;
}

void advance_ordinary_pc(R3000aState& state) noexcept {
    state.pc = state.next_pc;
    state.next_pc = state.next_pc + 4u;
}

R3000aStepResult boundary(
    R3000aState& state,
    R3000aBoundaryCode code,
    R3000aStage stage,
    std::uint32_t pc,
    std::optional<std::uint32_t> opcode = std::nullopt) noexcept {
    state.gpr[0] = 0u;
    R3000aStepResult result{};
    result.status = R3000aStepStatus::boundary;
    result.diagnostic.boundary = code;
    result.diagnostic.stage = stage;
    result.diagnostic.pc = pc;
    result.diagnostic.opcode = opcode;
    return result;
}

R3000aStepResult enter_exception(
    R3000aState& state,
    R3000aExceptionCode code,
    R3000aStage stage,
    std::uint32_t fault_pc,
    std::optional<std::uint32_t> opcode = std::nullopt,
    std::optional<std::uint32_t> bad_vaddr = std::nullopt) noexcept {
    const auto status_before = state.cop0.status;
    state.cop0.cause &= ~(kCauseExcCodeMask | kCauseContextMask);
    state.cop0.cause |= static_cast<std::uint32_t>(code) << 2;
    state.cop0.epc = fault_pc;

    if (code == R3000aExceptionCode::adel || code == R3000aExceptionCode::ades) {
        if (bad_vaddr) state.cop0.bad_vaddr = *bad_vaddr;
    }

    const auto low_stack = status_before & kStatusModeStackMask;
    state.cop0.status = (status_before & ~kStatusModeStackMask) |
                        ((low_stack << 2) & kStatusModeStackMask);

    const std::uint32_t vector = (status_before & kStatusBev) != 0u
        ? 0xBFC00180u
        : 0x80000080u;
    state.pc = vector;
    state.next_pc = vector + 4u;
    state.delay_slot = {};
    state.gpr[0] = 0u;

    R3000aStepResult result{};
    result.status = R3000aStepStatus::exception;
    result.diagnostic.stage = stage;
    result.diagnostic.pc = fault_pc;
    result.diagnostic.opcode = opcode;
    result.diagnostic.exception_code = code;
    if (bad_vaddr) {
        result.diagnostic.address = *bad_vaddr;
    }
    return result;
}

} // namespace

R3000aStepResult step_r3000a(R3000aState& state, R3000aBus& bus) noexcept {
    state.gpr[0] = 0u;
    const std::uint32_t instruction_pc = state.pc;

    if ((instruction_pc & 3u) != 0u) {
        auto result = enter_exception(
            state,
            R3000aExceptionCode::adel,
            R3000aStage::fetch,
            instruction_pc,
            std::nullopt,
            instruction_pc);
        result.diagnostic.access_width = 4u;
        return result;
    }

    const auto fetched = bus.read32(instruction_pc);
    if (fetched.status == R3000aBusStatus::unsupported) {
        auto result = boundary(
            state,
            R3000aBoundaryCode::unsupported_address_space,
            R3000aStage::fetch,
            instruction_pc);
        result.diagnostic.address = instruction_pc;
        result.diagnostic.access_width = 4u;
        return result;
    }
    if (fetched.status == R3000aBusStatus::bus_error) {
        auto result = enter_exception(
            state,
            R3000aExceptionCode::ibe,
            R3000aStage::fetch,
            instruction_pc);
        result.diagnostic.address = instruction_pc;
        result.diagnostic.access_width = 4u;
        return result;
    }

    const auto instruction = decode_mips(fetched.value);
    const std::uint32_t rs = state.gpr[instruction.rs];
    const std::uint32_t rt = state.gpr[instruction.rt];
    bool supported = true;

    switch (instruction.op) {
        case MipsOp::sll:
            write_gpr(state, instruction.rd, rt << instruction.sa);
            break;
        case MipsOp::srl:
            write_gpr(state, instruction.rd, rt >> instruction.sa);
            break;
        case MipsOp::sra:
            write_gpr(state, instruction.rd, arithmetic_shift_right(rt, instruction.sa));
            break;
        case MipsOp::sllv:
            write_gpr(state, instruction.rd, rt << (rs & 31u));
            break;
        case MipsOp::srlv:
            write_gpr(state, instruction.rd, rt >> (rs & 31u));
            break;
        case MipsOp::srav:
            write_gpr(state, instruction.rd, arithmetic_shift_right(rt, rs));
            break;
        case MipsOp::add: {
            const auto value = rs + rt;
            if (add_overflow(rs, rt, value)) {
                return enter_exception(
                    state,
                    R3000aExceptionCode::overflow,
                    R3000aStage::execute,
                    instruction_pc,
                    instruction.raw);
            }
            write_gpr(state, instruction.rd, value);
            break;
        }
        case MipsOp::addu:
            write_gpr(state, instruction.rd, rs + rt);
            break;
        case MipsOp::sub: {
            const auto value = rs - rt;
            if (sub_overflow(rs, rt, value)) {
                return enter_exception(
                    state,
                    R3000aExceptionCode::overflow,
                    R3000aStage::execute,
                    instruction_pc,
                    instruction.raw);
            }
            write_gpr(state, instruction.rd, value);
            break;
        }
        case MipsOp::subu:
            write_gpr(state, instruction.rd, rs - rt);
            break;
        case MipsOp::bit_and:
            write_gpr(state, instruction.rd, rs & rt);
            break;
        case MipsOp::bit_or:
            write_gpr(state, instruction.rd, rs | rt);
            break;
        case MipsOp::bit_xor:
            write_gpr(state, instruction.rd, rs ^ rt);
            break;
        case MipsOp::bit_nor:
            write_gpr(state, instruction.rd, ~(rs | rt));
            break;
        case MipsOp::slt:
            write_gpr(state, instruction.rd, signed_view(rs) < signed_view(rt) ? 1u : 0u);
            break;
        case MipsOp::sltu:
            write_gpr(state, instruction.rd, rs < rt ? 1u : 0u);
            break;
        case MipsOp::addiu:
            write_gpr(state, instruction.rt, rs + sign_extend16(instruction.immediate));
            break;
        case MipsOp::andi:
            write_gpr(state, instruction.rt, rs & static_cast<std::uint32_t>(instruction.immediate));
            break;
        case MipsOp::ori:
            write_gpr(state, instruction.rt, rs | static_cast<std::uint32_t>(instruction.immediate));
            break;
        case MipsOp::xori:
            write_gpr(state, instruction.rt, rs ^ static_cast<std::uint32_t>(instruction.immediate));
            break;
        case MipsOp::lui:
            write_gpr(state, instruction.rt, static_cast<std::uint32_t>(instruction.immediate) << 16);
            break;
        case MipsOp::slti: {
            const auto immediate = signed_view(sign_extend16(instruction.immediate));
            write_gpr(state, instruction.rt, signed_view(rs) < immediate ? 1u : 0u);
            break;
        }
        case MipsOp::sltiu:
            write_gpr(state, instruction.rt, rs < sign_extend16(instruction.immediate) ? 1u : 0u);
            break;
        case MipsOp::mfhi:
            write_gpr(state, instruction.rd, state.hi);
            break;
        case MipsOp::mthi:
            state.hi = rs;
            break;
        case MipsOp::mflo:
            write_gpr(state, instruction.rd, state.lo);
            break;
        case MipsOp::mtlo:
            state.lo = rs;
            break;
        case MipsOp::mult: {
            const auto lhs = static_cast<std::int64_t>(signed_view(rs));
            const auto rhs = static_cast<std::int64_t>(signed_view(rt));
            const auto product = lhs * rhs;
            const auto bits = static_cast<std::uint64_t>(product);
            state.lo = static_cast<std::uint32_t>(bits);
            state.hi = static_cast<std::uint32_t>(bits >> 32);
            break;
        }
        case MipsOp::multu: {
            const auto product = static_cast<std::uint64_t>(rs) * static_cast<std::uint64_t>(rt);
            state.lo = static_cast<std::uint32_t>(product);
            state.hi = static_cast<std::uint32_t>(product >> 32);
            break;
        }
        case MipsOp::div:
            if (rt == 0u) {
                state.hi = rs;
                state.lo = signed_view(rs) < 0 ? 1u : 0xFFFFFFFFu;
            } else if (rs == 0x80000000u && rt == 0xFFFFFFFFu) {
                state.hi = 0u;
                state.lo = 0x80000000u;
            } else {
                const auto lhs = static_cast<std::int64_t>(signed_view(rs));
                const auto rhs = static_cast<std::int64_t>(signed_view(rt));
                state.lo = static_cast<std::uint32_t>(lhs / rhs);
                state.hi = static_cast<std::uint32_t>(lhs % rhs);
            }
            break;
        case MipsOp::divu:
            if (rt == 0u) {
                state.hi = rs;
                state.lo = 0xFFFFFFFFu;
            } else {
                state.lo = rs / rt;
                state.hi = rs % rt;
            }
            break;
        case MipsOp::addi: {
            const auto immediate = sign_extend16(instruction.immediate);
            const auto value = rs + immediate;
            if (add_overflow(rs, immediate, value)) {
                return enter_exception(
                    state,
                    R3000aExceptionCode::overflow,
                    R3000aStage::execute,
                    instruction_pc,
                    instruction.raw);
            }
            write_gpr(state, instruction.rt, value);
            break;
        }
        case MipsOp::syscall:
            return enter_exception(
                state,
                R3000aExceptionCode::syscall,
                R3000aStage::execute,
                instruction_pc,
                instruction.raw);
        case MipsOp::break_:
            return enter_exception(
                state,
                R3000aExceptionCode::breakpoint,
                R3000aStage::execute,
                instruction_pc,
                instruction.raw);
        case MipsOp::reserved:
            return enter_exception(
                state,
                R3000aExceptionCode::reserved_instruction,
                R3000aStage::decode,
                instruction_pc,
                instruction.raw);
        default:
            supported = false;
            break;
    }

    if (!supported) {
        return boundary(
            state,
            R3000aBoundaryCode::architectural_operation_unimplemented,
            R3000aStage::execute,
            instruction_pc,
            instruction.raw);
    }

    advance_ordinary_pc(state);
    state.gpr[0] = 0u;
    return {};
}

} // namespace jojo
