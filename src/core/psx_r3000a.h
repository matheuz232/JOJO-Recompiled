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
};

inline void reset_psx_r3000a(PsxR3000aState& state, std::uint32_t entry_pc) noexcept {
    state = {};
    state.pc = entry_pc;
    state.next_pc = entry_pc + 4u;
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

    const auto branch_target = [&](std::uint16_t immediate) noexcept {
        const auto displacement = static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
        return instruction_pc + 4u + static_cast<std::uint32_t>(displacement);
    };

    if (op == 0u) {
        const auto rd = static_cast<std::uint8_t>((instruction >> 11u) & 0x1fu);
        const auto shamt = static_cast<std::uint8_t>((instruction >> 6u) & 0x1fu);
        const auto funct = static_cast<std::uint8_t>(instruction & 0x3fu);

        switch (funct) {
        case 0x00u: // SLL (also architectural NOP when instruction == 0)
            if (rd != 0u) state.gpr[rd] = state.gpr[rt] << shamt;
            supported = true;
            break;
        case 0x21u: // ADDU
            if (rd != 0u) state.gpr[rd] = state.gpr[rs] + state.gpr[rt];
            supported = true;
            break;
        case 0x23u: // SUBU
            if (rd != 0u) state.gpr[rd] = state.gpr[rs] - state.gpr[rt];
            supported = true;
            break;
        case 0x2bu: // SLTU
            if (rd != 0u) state.gpr[rd] = state.gpr[rs] < state.gpr[rt] ? 1u : 0u;
            supported = true;
            break;
        default:
            break;
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
    } else if (op == 0x09u) { // ADDIU
        const auto signed_immediate = static_cast<std::int32_t>(
            static_cast<std::int16_t>(instruction & 0xffffu));
        if (rt != 0u) {
            state.gpr[rt] = state.gpr[rs] + static_cast<std::uint32_t>(signed_immediate);
        }
        supported = true;
    } else if (op == 0x0fu) { // LUI
        if (rt != 0u) state.gpr[rt] = (instruction & 0xffffu) << 16u;
        supported = true;
    } else if (op == 0x03u) { // JAL
        const auto target = instruction & 0x03ffffffu;
        state.gpr[31] = instruction_pc + 8u;
        following_pc = ((instruction_pc + 4u) & 0xf0000000u) | (target << 2u);
        supported = true;
    }

    if (!supported) {
        return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, instruction};
    }

    state.gpr[0] = 0u;
    state.pc = sequential_pc;
    state.next_pc = following_pc;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_r3000a(
    PsxR3000aState& state, std::uint32_t instruction, PsxBus& bus) noexcept {
    const auto op = static_cast<std::uint8_t>(instruction >> 26u);
    if (op != 0x2bu) return step_psx_r3000a(state, instruction);

    const std::uint32_t instruction_pc = state.pc;
    const std::uint32_t sequential_pc = state.next_pc;
    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    const auto signed_immediate = static_cast<std::int32_t>(
        static_cast<std::int16_t>(instruction & 0xffffu));
    const auto address = state.gpr[rs] + static_cast<std::uint32_t>(signed_immediate);

    if (psx_bus_write_u32(bus, address, state.gpr[rt]) != PsxBusAccessReason::ok) {
        return {PsxR3000aStepReason::memory_fault, instruction_pc, instruction};
    }

    state.gpr[0] = 0u;
    state.pc = sequential_pc;
    state.next_pc = sequential_pc + 4u;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

}
