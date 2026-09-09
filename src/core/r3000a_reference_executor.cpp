#include "core/r3000a_reference_executor.h"

#include "core/mips_decoder.h"

#include <bit>
#include <cstdint>

namespace jojo {
namespace {

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

} // namespace

R3000aStepResult step_r3000a(R3000aState& state, R3000aBus& bus) noexcept {
    state.gpr[0] = 0u;
    const std::uint32_t instruction_pc = state.pc;

    if ((instruction_pc & 3u) != 0u) {
        auto result = boundary(
            state,
            R3000aBoundaryCode::architectural_operation_unimplemented,
            R3000aStage::fetch,
            instruction_pc);
        result.diagnostic.address = instruction_pc;
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
        auto result = boundary(
            state,
            R3000aBoundaryCode::architectural_operation_unimplemented,
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
        case MipsOp::addu:
            write_gpr(state, instruction.rd, rs + rt);
            break;
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
