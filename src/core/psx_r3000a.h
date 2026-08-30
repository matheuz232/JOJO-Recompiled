#pragma once
#include "core/psx_bus.h"
#include <array>
#include <cstdint>

namespace jojo {

enum class PsxR3000aStepReason {
    ok,
    unsupported_instruction,
    memory_fault,
    exception,
};

enum class PsxR3000aExceptionCode : std::uint8_t {
    interrupt = 0u,
    address_error_load = 4u,
    address_error_store = 5u,
    syscall = 8u,
    breakpoint = 9u,
    reserved_instruction = 10u,
    coprocessor_unusable = 11u,
    overflow = 12u,
    none = 0xffu,
};

struct PsxR3000aStepResult {
    PsxR3000aStepReason reason{PsxR3000aStepReason::ok};
    std::uint32_t instruction_pc{};
    std::uint32_t instruction{};
    PsxR3000aExceptionCode exception_code{PsxR3000aExceptionCode::none};
};

struct PsxR3000aCop0State {
    std::uint32_t bad_vaddr{};
    std::uint32_t status{};
    std::uint32_t cause{};
    std::uint32_t epc{};
    std::uint32_t prid{2u};
};

struct PsxR3000aState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t pc{};
    std::uint32_t next_pc{};
    PsxR3000aCop0State cop0{};
    bool current_instruction_is_branch_delay_slot{};
    std::uint32_t branch_pc{};
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

[[nodiscard]] inline bool psx_r3000a_interrupt_pending(
    const PsxR3000aState& state) noexcept {
    constexpr std::uint32_t current_interrupt_enable = 1u;
    constexpr std::uint32_t interrupt_mask = 0x0000ff00u;
    return (state.cop0.status & current_interrupt_enable) != 0u &&
           (state.cop0.status & state.cop0.cause & interrupt_mask) != 0u;
}

[[nodiscard]] inline PsxR3000aStepResult raise_psx_r3000a_exception(
    PsxR3000aState& state,
    PsxR3000aExceptionCode code,
    std::uint32_t instruction_pc,
    std::uint32_t instruction,
    bool has_bad_vaddr = false,
    std::uint32_t bad_vaddr = 0u) noexcept {
    constexpr std::uint32_t branch_delay_bit = 0x80000000u;
    constexpr std::uint32_t pending_interrupt_bits = 0x0000ff00u;
    constexpr std::uint32_t bootstrap_exception_vector_bit = 1u << 22u;

    const bool in_delay_slot = state.current_instruction_is_branch_delay_slot;
    state.cop0.epc = in_delay_slot ? state.branch_pc : instruction_pc;
    state.cop0.cause = (state.cop0.cause & pending_interrupt_bits) |
                       (static_cast<std::uint32_t>(code) << 2u) |
                       (in_delay_slot ? branch_delay_bit : 0u);
    if (has_bad_vaddr) state.cop0.bad_vaddr = bad_vaddr;

    const bool bootstrap_vector =
        (state.cop0.status & bootstrap_exception_vector_bit) != 0u;
    state.cop0.status = (state.cop0.status & ~0x3fu) |
                        ((state.cop0.status << 2u) & 0x3cu);

    complete_psx_pending_load(state);
    state.gpr[0] = 0u;
    state.current_instruction_is_branch_delay_slot = false;
    state.branch_pc = 0u;
    state.pc = bootstrap_vector ? 0xbfc00180u : 0x80000080u;
    state.next_pc = state.pc + 4u;
    return {PsxR3000aStepReason::exception, instruction_pc, instruction, code};
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
    bool creates_branch_delay_slot = false;
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
            creates_branch_delay_slot = true;
            break;
        case 0x09u: { // JALR
            const auto target = state.gpr[rs];
            write_gpr(rd, instruction_pc + 8u);
            following_pc = target;
            supported = true;
            creates_branch_delay_slot = true;
            break;
        }
        case 0x10u: // MFHI
            write_gpr(rd, state.hi);
            supported = true;
            break;
        case 0x11u: // MTHI
            state.hi = state.gpr[rs];
            supported = true;
            break;
        case 0x12u: // MFLO
            write_gpr(rd, state.lo);
            supported = true;
            break;
        case 0x13u: // MTLO
            state.lo = state.gpr[rs];
            supported = true;
            break;
        case 0x18u: { // MULT
            const auto product = signed_value(state.gpr[rs]) * signed_value(state.gpr[rt]);
            const auto bits = static_cast<std::uint64_t>(product);
            state.lo = static_cast<std::uint32_t>(bits);
            state.hi = static_cast<std::uint32_t>(bits >> 32u);
            supported = true;
            break;
        }
        case 0x19u: { // MULTU
            const auto product = static_cast<std::uint64_t>(state.gpr[rs]) *
                                 static_cast<std::uint64_t>(state.gpr[rt]);
            state.lo = static_cast<std::uint32_t>(product);
            state.hi = static_cast<std::uint32_t>(product >> 32u);
            supported = true;
            break;
        }
        case 0x1au: { // DIV
            const auto dividend = signed_value(state.gpr[rs]);
            const auto divisor = signed_value(state.gpr[rt]);
            if (divisor == 0) {
                state.lo = dividend < 0 ? 1u : 0xffffffffu;
                state.hi = state.gpr[rs];
            } else if (dividend == -0x80000000ll && divisor == -1) {
                state.lo = 0x80000000u;
                state.hi = 0u;
            } else {
                state.lo = static_cast<std::uint32_t>(dividend / divisor);
                state.hi = static_cast<std::uint32_t>(dividend % divisor);
            }
            supported = true;
            break;
        }
        case 0x1bu: // DIVU
            if (state.gpr[rt] == 0u) {
                state.lo = 0xffffffffu;
                state.hi = state.gpr[rs];
            } else {
                state.lo = state.gpr[rs] / state.gpr[rt];
                state.hi = state.gpr[rs] % state.gpr[rt];
            }
            supported = true;
            break;
        case 0x20u: { // ADD
            const auto sum = signed_value(state.gpr[rs]) + signed_value(state.gpr[rt]);
            if (sum < -0x80000000ll || sum > 0x7fffffffll) {
                return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, instruction};
            }
            write_gpr(rd, static_cast<std::uint32_t>(sum));
            supported = true;
            break;
        }
        case 0x21u: // ADDU
            write_gpr(rd, state.gpr[rs] + state.gpr[rt]);
            supported = true;
            break;
        case 0x22u: { // SUB
            const auto difference = signed_value(state.gpr[rs]) - signed_value(state.gpr[rt]);
            if (difference < -0x80000000ll || difference > 0x7fffffffll) {
                return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, instruction};
            }
            write_gpr(rd, static_cast<std::uint32_t>(difference));
            supported = true;
            break;
        }
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
        creates_branch_delay_slot = supported;
    } else if (op == 0x04u) { // BEQ
        if (state.gpr[rs] == state.gpr[rt]) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
        creates_branch_delay_slot = true;
    } else if (op == 0x05u) { // BNE
        if (state.gpr[rs] != state.gpr[rt]) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
        creates_branch_delay_slot = true;
    } else if (op == 0x06u && rt == 0u) { // BLEZ
        if (signed_value(state.gpr[rs]) <= 0) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
        creates_branch_delay_slot = true;
    } else if (op == 0x07u && rt == 0u) { // BGTZ
        if (signed_value(state.gpr[rs]) > 0) {
            following_pc = branch_target(static_cast<std::uint16_t>(instruction));
        }
        supported = true;
        creates_branch_delay_slot = true;
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
        creates_branch_delay_slot = true;
    } else if (op == 0x03u) { // JAL
        const auto target = instruction & 0x03ffffffu;
        write_gpr(31u, instruction_pc + 8u);
        following_pc = ((instruction_pc + 4u) & 0xf0000000u) | (target << 2u);
        supported = true;
        creates_branch_delay_slot = true;
    }

    if (!supported) {
        return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, instruction};
    }

    complete_psx_pending_load(state, written_register);
    state.gpr[0] = 0u;
    state.pc = sequential_pc;
    state.next_pc = following_pc;
    state.current_instruction_is_branch_delay_slot = creates_branch_delay_slot;
    state.branch_pc = creates_branch_delay_slot ? instruction_pc : 0u;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_r3000a(
    PsxR3000aState& state, std::uint32_t instruction, PsxBus& bus) noexcept {
    const auto op = static_cast<std::uint8_t>(instruction >> 26u);
    if (op != 0x20u && op != 0x21u && op != 0x22u && op != 0x23u &&
        op != 0x24u && op != 0x25u && op != 0x26u && op != 0x28u &&
        op != 0x29u && op != 0x2au && op != 0x2bu && op != 0x2eu) {
        return step_psx_r3000a(state, instruction);
    }

    const std::uint32_t instruction_pc = state.pc;
    const std::uint32_t sequential_pc = state.next_pc;
    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    const auto signed_immediate = static_cast<std::int32_t>(
        static_cast<std::int16_t>(instruction & 0xffffu));
    const auto address = state.gpr[rs] + static_cast<std::uint32_t>(signed_immediate);

    if (op == 0x28u || op == 0x29u || op == 0x2bu) { // SB / SH / SW
        PsxBusAccessReason store_reason = PsxBusAccessReason::unmapped;
        if (op == 0x28u) {
            store_reason = psx_bus_write_u8(bus, address, static_cast<std::uint8_t>(state.gpr[rt]));
        } else if (op == 0x29u) {
            store_reason = psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(state.gpr[rt]));
        } else {
            store_reason = psx_bus_write_u32(bus, address, state.gpr[rt]);
        }
        if (store_reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, instruction};
        }

        complete_psx_pending_load(state);
        state.gpr[0] = 0u;
        state.pc = sequential_pc;
        state.next_pc = sequential_pc + 4u;
        state.current_instruction_is_branch_delay_slot = false;
        state.branch_pc = 0u;
        return {PsxR3000aStepReason::ok, instruction_pc, instruction};
    }

    if (op == 0x2au || op == 0x2eu) { // SWL / SWR
        const auto aligned_address = address & ~std::uint32_t{3};
        const auto current = psx_bus_read_u32(bus, aligned_address);
        if (current.reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, instruction};
        }

        const auto source = state.gpr[rt];
        std::uint32_t merged = current.value;
        if (op == 0x2au) {
            switch (address & 3u) {
            case 0u: merged = (current.value & 0xffffff00u) | (source >> 24u); break;
            case 1u: merged = (current.value & 0xffff0000u) | (source >> 16u); break;
            case 2u: merged = (current.value & 0xff000000u) | (source >> 8u); break;
            case 3u: merged = source; break;
            }
        } else {
            switch (address & 3u) {
            case 0u: merged = source; break;
            case 1u: merged = (current.value & 0x000000ffu) | (source << 8u); break;
            case 2u: merged = (current.value & 0x0000ffffu) | (source << 16u); break;
            case 3u: merged = (current.value & 0x00ffffffu) | (source << 24u); break;
            }
        }

        if (psx_bus_write_u32(bus, aligned_address, merged) != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, instruction};
        }
        complete_psx_pending_load(state);
        state.gpr[0] = 0u;
        state.pc = sequential_pc;
        state.next_pc = sequential_pc + 4u;
        state.current_instruction_is_branch_delay_slot = false;
        state.branch_pc = 0u;
        return {PsxR3000aStepReason::ok, instruction_pc, instruction};
    }

    PsxBusAccessReason load_reason = PsxBusAccessReason::ok;
    std::uint32_t load_value = 0u;
    if (op == 0x22u || op == 0x26u) { // LWL / LWR
        const auto loaded = psx_bus_read_u32(bus, address & ~std::uint32_t{3});
        load_reason = loaded.reason;
        const auto merge_source = state.pending_load_valid &&
                                  state.pending_load_register == rt
            ? state.pending_load_value
            : state.gpr[rt];
        if (op == 0x22u) {
            switch (address & 3u) {
            case 0u: load_value = (merge_source & 0x00ffffffu) | (loaded.value << 24u); break;
            case 1u: load_value = (merge_source & 0x0000ffffu) | (loaded.value << 16u); break;
            case 2u: load_value = (merge_source & 0x000000ffu) | (loaded.value << 8u); break;
            case 3u: load_value = loaded.value; break;
            }
        } else {
            switch (address & 3u) {
            case 0u: load_value = loaded.value; break;
            case 1u: load_value = (merge_source & 0xff000000u) | (loaded.value >> 8u); break;
            case 2u: load_value = (merge_source & 0xffff0000u) | (loaded.value >> 16u); break;
            case 3u: load_value = (merge_source & 0xffffff00u) | (loaded.value >> 24u); break;
            }
        }
    } else if (op == 0x20u || op == 0x24u) { // LB / LBU
        const auto loaded = psx_bus_read_u8(bus, address);
        load_reason = loaded.reason;
        load_value = static_cast<std::uint32_t>(loaded.value);
        if (op == 0x20u && (loaded.value & 0x80u) != 0u) {
            load_value |= 0xffffff00u;
        }
    } else if (op == 0x21u || op == 0x25u) { // LH / LHU
        const auto loaded = psx_bus_read_u16(bus, address);
        load_reason = loaded.reason;
        load_value = static_cast<std::uint32_t>(loaded.value);
        if (op == 0x21u && (loaded.value & 0x8000u) != 0u) {
            load_value |= 0xffff0000u;
        }
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
    state.current_instruction_is_branch_delay_slot = false;
    state.branch_pc = 0u;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

}
