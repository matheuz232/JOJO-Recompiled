#pragma once
#include "core/psx_bus.h"
#include <array>
#include <cstdint>

namespace jojo {

enum class PsxR3000aStepReason {
    ok,
    unsupported_instruction,
    memory_fault,
};

struct PsxR3000aStepResult {
    PsxR3000aStepReason reason{PsxR3000aStepReason::ok};
    std::uint32_t instruction_pc{};
    std::uint32_t instruction{};
};

struct PsxR3000aState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t pc{};
    std::uint32_t next_pc{};
    bool pending_load_valid{};
    std::uint8_t pending_load_register{};
    std::uint32_t pending_load_value{};
};

inline void reset_psx_r3000a(PsxR3000aState& state, std::uint32_t entry_pc) noexcept {
    state = {};
    state.pc = entry_pc;
    state.next_pc = entry_pc + 4u;
}

inline void complete_psx_pending_load(PsxR3000aState& state,
                                      std::uint8_t written_register = 0xffu) noexcept {
    if (state.pending_load_valid &&
        state.pending_load_register != 0u &&
        state.pending_load_register != written_register) {
        state.gpr[state.pending_load_register] = state.pending_load_value;
    }
    state.pending_load_valid = false;
    state.pending_load_register = 0u;
    state.pending_load_value = 0u;
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_r3000a(
    PsxR3000aState& state, std::uint32_t instruction) noexcept {
    const std::uint32_t instruction_pc = state.pc;
    const std::uint32_t sequential_pc = state.next_pc;
    std::uint32_t following_pc = sequential_pc + 4u;

    const auto op = static_cast<std::uint8_t>(instruction >> 26u);
    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    bool supported = false;
    std::uint8_t written_register = 0xffu;

    const auto write_gpr = [&](std::uint8_t reg, std::uint32_t value) noexcept {
        if (reg == 0u) return;
        state.gpr[reg] = value;
        written_register = reg;
    };

    const auto branch_target = [&](std::uint16_t immediate) noexcept {
        const auto displacement = static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
        return instruction_pc + 4u + static_cast<std::uint32_t>(displacement);
    };

    const auto signed_value = [](std::uint32_t value) noexcept -> std::int64_t {
        return value <= 0x7fffffffu
            ? static_cast<std::int64_t>(value)
            : static_cast<std::int64_t>(value) - 0x100000000ll;
    };

    const auto arithmetic_shift_right = [](std::uint32_t value,
                                            std::uint32_t shift) noexcept {
        shift &= 0x1fu;
        if (shift == 0u) return value;
        const auto shifted = value >> shift;
        if ((value & 0x80000000u) == 0u) return shifted;
        return shifted | (~std::uint32_t{0} << (32u - shift));
    };

    if (op == 0u) {
        const auto rd = static_cast<std::uint8_t>((instruction >> 11u) & 0x1fu);
        const auto shamt = static_cast<std::uint8_t>((instruction >> 6u) & 0x1fu);
        const auto funct = static_cast<std::uint8_t>(instruction & 0x3fu);

        switch (funct) {
        case 0x00u: // SLL (also architectural NOP when instruction == 0)
            write_gpr(rd, state.gpr[rt] << shamt);
            supported = true;
            break;
        case 0x02u: // SRL
            write_gpr(rd, state.gpr[rt] >> shamt);
            supported = true;
            break;
        case 0x03u: // SRA
            write_gpr(rd, arithmetic_shift_right(state.gpr[rt], shamt));
            supported = true;
            break;
        case 0x04u: // SLLV
            write_gpr(rd, state.gpr[rt] << (state.gpr[rs] & 0x1fu));
            supported = true;
            break;
        case 0x06u: // SRLV
            write_gpr(rd, state.gpr[rt] >> (state.gpr[rs] & 0x1fu));
            supported = true;
            break;
        case 0x07u: // SRAV
            write_gpr(rd, arithmetic_shift_right(state.gpr[rt], state.gpr[rs]));
            supported = true;
            break;
        case 0x08u: // JR
            following_pc = state.gpr[rs];
            supported = true;
            break;
        case 0x09u: { // JALR
            const auto target = state.gpr[rs];
            write_gpr(rd, instruction_pc + 8u);
            following_pc = target;
            supported = true;
            break;
        }
        case 0x21u: // ADDU
            write_gpr(rd, state.gpr[rs] + state.gpr[rt]);
            supported = true;
            break;
        case 0x23u: // SUBU
            write_gpr(rd, state.gpr[rs] - state.gpr[rt]);
            supported = true;
            break;
        case 0x24u: // AND
            write_gpr(rd, state.gpr[rs] & state.gpr[rt]);
            supported = true;
            break;
        case 0x25u: // OR
            write_gpr(rd, state.gpr[rs] | state.gpr[rt]);
            supported = true;
            break;
        case 0x26u: // XOR
            write_gpr(rd, state.gpr[rs] ^ state.gpr[rt]);
            supported = true;
            break;
        case 0x27u: // NOR
            write_gpr(rd, ~(state.gpr[rs] | state.gpr[rt]));
            supported = true;
            break;
        case 0x2au: // SLT
            write_gpr(rd, signed_value(state.gpr[rs]) < signed_value(state.gpr[rt]) ? 1u : 0u);
            supported = true;
            break;
        case 0x2bu: // SLTU
            write_gpr(rd, state.gpr[rs] < state.gpr[rt] ? 1u : 0u);
            supported = true;
            break;
        default:
            break;
        }
    } else if (op == 0x01u) { // REGIMM branches
        bool take_branch = false;
        switch (rt) {
        case 0x00u: // BLTZ
            take_branch = signed_value(state.gpr[rs]) < 0;
            supported = true;
            break;
        case 0x01u: // BGEZ
            take_branch = signed_value(state.gpr[rs]) >= 0;
            supported = true;
            break;
        case 0x10u: // BLTZAL
            take_branch = signed_value(state.gpr[rs]) < 0;
            write_gpr(31u, instruction_pc + 8u);
            supported = true;
            break;
        case 0x11u: // BGEZAL
            take_branch = signed_value(state.gpr[rs]) >= 0;
            write_gpr(31u, instruction_pc + 8u);
            supported = true;
            break;
        default:
            break;
        }
        if (supported && take_branch) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
    } else if (op == 0x04u) { // BEQ
        if (state.gpr[rs] == state.gpr[rt]) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
    } else if (op == 0x05u) { // BNE
        if (state.gpr[rs] != state.gpr[rt]) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
    } else if (op == 0x06u && rt == 0u) { // BLEZ
        if (signed_value(state.gpr[rs]) <= 0) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
    } else if (op == 0x07u && rt == 0u) { // BGTZ
        if (signed_value(state.gpr[rs]) > 0) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
    } else if (op == 0x08u) { // ADDI (overflow trap deferred until COP0 exists)
        const auto raw_immediate = static_cast<std::uint32_t>(instruction & 0xffffu);
        const std::int64_t lhs = state.gpr[rs] <= 0x7fffffffu
            ? static_cast<std::int64_t>(state.gpr[rs])
            : static_cast<std::int64_t>(state.gpr[rs]) - 0x100000000ll;
        const std::int64_t rhs = (raw_immediate & 0x8000u) == 0u
            ? static_cast<std::int64_t>(raw_immediate)
            : static_cast<std::int64_t>(raw_immediate) - 0x10000ll;
        const std::int64_t sum = lhs + rhs;
        if (sum < -0x80000000ll || sum > 0x7fffffffll) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, instruction};
        }
        write_gpr(rt, static_cast<std::uint32_t>(sum));
        supported = true;
    } else if (op == 0x09u) { // ADDIU
        const auto signed_immediate = static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xffffu));
        write_gpr(rt, state.gpr[rs] + static_cast<std::uint32_t>(signed_immediate));
        supported = true;
    } else if (op == 0x0au) { // SLTI
        const auto immediate = static_cast<std::int16_t>(instruction & 0xffffu);
        write_gpr(rt, signed_value(state.gpr[rs]) < static_cast<std::int64_t>(immediate) ? 1u : 0u);
        supported = true;
    } else if (op == 0x0bu) { // SLTIU
        const auto immediate = static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xffffu));
        write_gpr(rt, state.gpr[rs] < static_cast<std::uint32_t>(immediate) ? 1u : 0u);
        supported = true;
    } else if (op == 0x0cu) { // ANDI
        write_gpr(rt, state.gpr[rs] & (instruction & 0xffffu));
        supported = true;
    } else if (op == 0x0du) { // ORI
        write_gpr(rt, state.gpr[rs] | (instruction & 0xffffu));
        supported = true;
    } else if (op == 0x0eu) { // XORI
        write_gpr(rt, state.gpr[rs] ^ (instruction & 0xffffu));
        supported = true;
    } else if (op == 0x0fu) { // LUI
        write_gpr(rt, (instruction & 0xffffu) << 16u);
        supported = true;
    } else if (op == 0x02u) { // J
        const auto target = instruction & 0x03ffffffu;
        following_pc = ((instruction_pc + 4u) & 0xf0000000u) | (target << 2u);
        supported = true;
    } else if (op == 0x03u) { // JAL
        const auto target = instruction & 0x03ffffffu;
        write_gpr(31u, instruction_pc + 8u);
        following_pc = ((instruction_pc + 4u) & 0xf0000000u) | (target << 2u);
        supported = true;
    }

    if (!supported) {
        return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, instruction};
    }

    complete_psx_pending_load(state, written_register);
    state.gpr[0] = 0u;
    state.pc = sequential_pc;
    state.next_pc = following_pc;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_r3000a(
    PsxR3000aState& state, std::uint32_t instruction, PsxBus& bus) noexcept {
    const auto op = static_cast<std::uint8_t>(instruction >> 26u);
    if (op != 0x23u && op != 0x25u && op != 0x29u && op != 0x2bu) {
        return step_psx_r3000a(state, instruction);
    }

    const std::uint32_t instruction_pc = state.pc;
    const std::uint32_t sequential_pc = state.next_pc;
    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    const auto signed_immediate = static_cast<std::int32_t>(
        static_cast<std::int16_t>(instruction & 0xffffu));
    const auto address = state.gpr[rs] + static_cast<std::uint32_t>(signed_immediate);

    if (op == 0x29u || op == 0x2bu) { // SH / SW
        const auto store_reason = op == 0x29u
            ? psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(state.gpr[rt]))
            : psx_bus_write_u32(bus, address, state.gpr[rt]);
        if (store_reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, instruction};
        }

        complete_psx_pending_load(state);
        state.gpr[0] = 0u;
        state.pc = sequential_pc;
        state.next_pc = sequential_pc + 4u;
        return {PsxR3000aStepReason::ok, instruction_pc, instruction};
    }

    PsxBusAccessReason load_reason = PsxBusAccessReason::ok;
    std::uint32_t load_value = 0u;
    if (op == 0x25u) { // LHU
        const auto loaded = psx_bus_read_u16(bus, address);
        load_reason = loaded.reason;
        load_value = static_cast<std::uint32_t>(loaded.value);
    } else { // LW
        const auto loaded = psx_bus_read_u32(bus, address);
        load_reason = loaded.reason;
        load_value = loaded.value;
    }

    if (load_reason != PsxBusAccessReason::ok) {
        return {PsxR3000aStepReason::memory_fault, instruction_pc, instruction};
    }

    const bool previous_load_valid = state.pending_load_valid;
    const auto previous_load_register = state.pending_load_register;
    const auto previous_load_value = state.pending_load_value;

    if (previous_load_valid &&
        previous_load_register != 0u &&
        previous_load_register != rt) {
        state.gpr[previous_load_register] = previous_load_value;
    }

    state.pending_load_valid = rt != 0u;
    state.pending_load_register = rt;
    state.pending_load_value = load_value;
    state.gpr[0] = 0u;
    state.pc = sequential_pc;
    state.next_pc = sequential_pc + 4u;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

}
