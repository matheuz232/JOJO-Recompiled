#pragma once
#include "core/psx_bus.h"
#include "core/psx_exe.h"
#include "core/psx_gte.h"
#include "core/psx_r3000a.h"
#include "core/psx_system_cnf.h"
#include "core/result.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace jojo {

struct PsxBiosState {
    bool heap_initialized{};
    std::uint32_t heap_base{};
    std::uint32_t heap_size{};
    bool entry_interrupt_hook_installed{};
    std::uint32_t entry_interrupt_hook_address{};
    bool pad_card_irq_completes{true};
    std::array<bool, 4> timer_vblank_irq_auto_ack{true, true, true, true};
    bool cdrom_irq_handlers_installed{true};
    bool c0_table_materialized{};
    bool b0_table_materialized{};
    bool card_initialized{};
    bool card_started{};
    bool card_pad_enabled{};
    bool early_card_irq_installed{};
};

struct PsxRuntime {
    PsxBus bus{};
    PsxR3000aState cpu{};
    PsxGteState gte{};
    PsxBiosState bios{};
};

[[nodiscard]] inline Result<void> load_psx_boot_executable(
    PsxRuntime& runtime,
    std::span<const std::uint8_t> file,
    const PsxSystemCnf& system) {
    const auto parsed = parse_psx_exe(file);
    if (!parsed) return Result<void>::failure(parsed.error, parsed.detail);

    constexpr std::size_t header_size = 0x800u;
    const auto& exe = parsed.value;

    std::uint32_t physical_start = 0;
    if (!psx_bus_virtual_to_physical(exe.load_address, physical_start) ||
        physical_start >= PsxBus::default_ram_mirror_window) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS-X EXE load address is outside supported PS1 main RAM");
    }

    const auto physical_offset = physical_start & static_cast<std::uint32_t>(PsxBus::main_ram_size - 1u);
    const auto payload_size = static_cast<std::size_t>(exe.payload_size);
    if (static_cast<std::size_t>(physical_offset) + payload_size > PsxBus::main_ram_size) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS-X EXE payload would wrap across mirrored main RAM");
    }

    std::fill(runtime.bus.ram.begin(), runtime.bus.ram.end(), std::uint8_t{0});
    for (std::size_t i = 0; i < payload_size; ++i) {
        runtime.bus.ram[static_cast<std::size_t>(physical_offset) + i] = file[header_size + i];
    }

    reset_psx_r3000a(runtime.cpu, exe.initial_pc);
    runtime.cpu.gpr[28] = exe.initial_gp;

    std::uint32_t stack = system.stack;
    if (stack == 0u && exe.stack_base != 0u) {
        const auto sum = static_cast<std::uint64_t>(exe.stack_base) +
                         static_cast<std::uint64_t>(exe.stack_offset);
        if (sum > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "PS-X EXE stack base plus offset overflows 32-bit address space");
        }
        stack = static_cast<std::uint32_t>(sum);
    }
    runtime.cpu.gpr[29] = stack;
    runtime.cpu.gpr[30] = stack;
    runtime.cpu.gpr[0] = 0u;

    return Result<void>::success();
}

[[nodiscard]] inline bool is_psx_bios_vector(std::uint32_t pc) noexcept {
    return pc == 0x000000a0u || pc == 0x000000b0u || pc == 0x000000c0u;
}

inline void return_from_psx_bios_call(PsxRuntime& runtime) noexcept {
    const auto return_pc = runtime.cpu.gpr[31];
    runtime.cpu.pc = return_pc;
    runtime.cpu.next_pc = return_pc + 4u;
    runtime.cpu.gpr[0] = 0u;
}

[[nodiscard]] inline bool restore_scph1001_default_entry_interrupt(PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t default_entry_address = 0x00006cf4u;
    constexpr std::array<std::uint32_t, 12> default_exit_structure{
        0x00000f40u, 0x000085d4u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u,
    };

    for (std::size_t i = 0; i < default_exit_structure.size(); ++i) {
        const auto address = default_entry_address + static_cast<std::uint32_t>(i * 4u);
        if (psx_bus_write_u32(runtime.bus, address, default_exit_structure[i]) !=
            PsxBusAccessReason::ok) {
            return false;
        }
    }

    runtime.bios.entry_interrupt_hook_installed = false;
    runtime.bios.entry_interrupt_hook_address = default_entry_address;
    runtime.cpu.gpr[2] = default_entry_address;
    return true;
}

[[nodiscard]] inline bool materialize_scph1001_c0_patch_surface(PsxRuntime& runtime) noexcept {
    if (runtime.bios.c0_table_materialized) return true;

    constexpr std::uint32_t c0_table_address = 0x00000674u;
    constexpr std::uint32_t exception_handler_address = 0x00000c80u;
    constexpr std::uint32_t exception_patch_address = exception_handler_address + 0x28u;
    constexpr std::array<std::uint32_t, 6> exception_patch_surface{
        0xaf410004u, 0xaf420008u, 0xaf43000cu,
        0xaf5f007cu, 0x40037000u, 0x00000000u,
    };

    if (psx_bus_write_u32(runtime.bus, c0_table_address + 6u * 4u,
                          exception_handler_address) != PsxBusAccessReason::ok) {
        return false;
    }
    for (std::size_t i = 0; i < exception_patch_surface.size(); ++i) {
        const auto address = exception_patch_address + static_cast<std::uint32_t>(i * 4u);
        if (psx_bus_write_u32(runtime.bus, address, exception_patch_surface[i]) !=
            PsxBusAccessReason::ok) {
            return false;
        }
    }

    runtime.bios.c0_table_materialized = true;
    return true;
}

[[nodiscard]] inline bool materialize_scph1001_b0_card_patch_surface(PsxRuntime& runtime) noexcept {
    if (runtime.bios.b0_table_materialized) return true;

    constexpr std::uint32_t b0_table_address = 0x00000874u;
    constexpr std::uint32_t change_clear_pad_address = 0x000043d0u;
    constexpr std::uint32_t card_delay_patch_address = change_clear_pad_address + 0x09c8u;
    constexpr std::array<std::uint32_t, 5> card_delay_patch_surface{
        0x946f000au, 0x3c080000u, 0x01e2c025u, 0x37190012u, 0xa479000au,
    };

    if (psx_bus_write_u32(runtime.bus, b0_table_address + 0x5bu * 4u,
                          change_clear_pad_address) != PsxBusAccessReason::ok) {
        return false;
    }
    for (std::size_t i = 0; i < card_delay_patch_surface.size(); ++i) {
        const auto address = card_delay_patch_address + static_cast<std::uint32_t>(i * 4u);
        if (psx_bus_write_u32(runtime.bus, address, card_delay_patch_surface[i]) !=
            PsxBusAccessReason::ok) {
            return false;
        }
    }

    runtime.bios.b0_table_materialized = true;
    return true;
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_gte_transfer(
    PsxRuntime& runtime, std::uint32_t instruction) noexcept {
    constexpr std::uint32_t cop2_enable = 1u << 30u;
    constexpr std::uint32_t cause_coprocessor_mask = 3u << 28u;
    constexpr std::uint32_t cause_coprocessor_2 = 2u << 28u;

    auto& cpu = runtime.cpu;
    const auto instruction_pc = cpu.pc;

    if ((cpu.cop0.status & cop2_enable) == 0u) {
        auto result = raise_psx_r3000a_exception(
            cpu, PsxR3000aExceptionCode::coprocessor_unusable,
            instruction_pc, instruction);
        cpu.cop0.cause = (cpu.cop0.cause & ~cause_coprocessor_mask) |
                         cause_coprocessor_2;
        return result;
    }

    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    const auto rd = static_cast<std::uint8_t>((instruction >> 11u) & 0x1fu);
    const bool canonical_move = (instruction & 0x7ffu) == 0u;
    const bool is_read = rs == 0x00u || rs == 0x02u;
    const bool is_write = rs == 0x04u || rs == 0x06u;

    if (!canonical_move || (!is_read && !is_write)) {
        return {PsxR3000aStepReason::unsupported_instruction,
                instruction_pc, instruction};
    }

    const auto sequential_pc = cpu.next_pc;
    const auto following_pc = sequential_pc + 4u;

    if (is_read) {
        const auto value = rs == 0x00u
            ? psx_gte_read_data(runtime.gte, rd)
            : psx_gte_read_control(runtime.gte, rd);

        const bool previous_load_valid = cpu.pending_load_valid;
        const auto previous_load_register = cpu.pending_load_register;
        const auto previous_load_value = cpu.pending_load_value;
        if (previous_load_valid &&
            previous_load_register != 0u &&
            previous_load_register != rt) {
            cpu.gpr[previous_load_register] = previous_load_value;
        }

        cpu.pending_load_valid = rt != 0u;
        cpu.pending_load_register = rt;
        cpu.pending_load_value = value;
    } else {
        if (rs == 0x04u) {
            psx_gte_write_data(runtime.gte, rd, cpu.gpr[rt]);
        } else {
            psx_gte_write_control(runtime.gte, rd, cpu.gpr[rt]);
        }
        complete_psx_pending_load(cpu);
    }

    cpu.gpr[0] = 0u;
    cpu.pc = sequential_pc;
    cpu.next_pc = following_pc;
    cpu.current_instruction_is_branch_delay_slot = false;
    cpu.branch_pc = 0u;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

[[nodiscard]] inline bool handle_psx_syscall_exception(PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t branch_delay_bit = 0x80000000u;
    constexpr std::uint32_t previous_interrupt_enable = 1u << 2u;
    constexpr std::uint32_t interrupt_mask_bit_2 = 1u << 10u;
    constexpr std::uint32_t critical_bits = previous_interrupt_enable | interrupt_mask_bit_2;

    if ((runtime.cpu.cop0.cause & branch_delay_bit) != 0u) return false;

    switch (runtime.cpu.gpr[4]) {
    case 0u:
        break;
    case 1u: {
        const bool both_were_enabled =
            (runtime.cpu.cop0.status & critical_bits) == critical_bits;
        runtime.cpu.cop0.status &= ~critical_bits;
        runtime.cpu.gpr[2] = both_were_enabled ? 1u : 0u;
        break;
    }
    case 2u:
        runtime.cpu.cop0.status |= critical_bits;
        break;
    default:
        return false;
    }

    runtime.cpu.cop0.status = (runtime.cpu.cop0.status & ~0x0fu) |
                              ((runtime.cpu.cop0.status >> 2u) & 0x0fu);
    const auto return_pc = runtime.cpu.cop0.epc + 4u;
    runtime.cpu.pc = return_pc;
    runtime.cpu.next_pc = return_pc + 4u;
    runtime.cpu.current_instruction_is_branch_delay_slot = false;
    runtime.cpu.branch_pc = 0u;
    runtime.cpu.gpr[0] = 0u;
    return true;
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_runtime(PsxRuntime& runtime) noexcept {
    const auto instruction_pc = runtime.cpu.pc;

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x44u) {
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x49u) {
        const auto write_reason = psx_bus_write_u32(
            runtime.bus, PsxBus::gpu_gp0_address, runtime.cpu.gpr[4]);
        if (write_reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x39u) {
        runtime.bios.heap_initialized = true;
        runtime.bios.heap_base = runtime.cpu.gpr[4];
        runtime.bios.heap_size = runtime.cpu.gpr[5];
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x72u) {
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x18u) {
        if (!restore_scph1001_default_entry_interrupt(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x19u) {
        runtime.bios.entry_interrupt_hook_installed = true;
        runtime.bios.entry_interrupt_hook_address = runtime.cpu.gpr[4];
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x35u) {
        const auto fd = runtime.cpu.gpr[4];
        const auto source = runtime.cpu.gpr[5];
        const auto length = runtime.cpu.gpr[6];
        if (fd != 1u) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }

        const auto end = static_cast<std::uint64_t>(source) +
                         static_cast<std::uint64_t>(length);
        if (end > (std::uint64_t{1} << 32u)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        for (std::uint32_t i = 0; i < length; ++i) {
            if (psx_bus_read_u8(runtime.bus, source + i).reason !=
                PsxBusAccessReason::ok) {
                return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
            }
        }

        runtime.cpu.gpr[2] = length;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x4au) {
        runtime.bios.card_initialized = true;
        runtime.bios.card_started = false;
        runtime.bios.card_pad_enabled = runtime.cpu.gpr[4] != 0u;
        runtime.bios.early_card_irq_installed = true;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x56u) {
        constexpr std::uint32_t c0_table_address = 0x00000674u;
        if (!materialize_scph1001_c0_patch_surface(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = c0_table_address;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x57u) {
        constexpr std::uint32_t b0_table_address = 0x00000874u;
        if (!materialize_scph1001_b0_card_patch_surface(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = b0_table_address;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x5bu) {
        runtime.bios.pad_card_irq_completes = runtime.cpu.gpr[4] != 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000c0u && runtime.cpu.gpr[9] == 0x0au) {
        const auto timer = runtime.cpu.gpr[4];
        if (timer > 3u) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }
        const auto index = static_cast<std::size_t>(timer);
        const bool previous = runtime.bios.timer_vblank_irq_auto_ack[index];
        runtime.bios.timer_vblank_irq_auto_ack[index] = runtime.cpu.gpr[5] != 0u;
        runtime.cpu.gpr[2] = previous ? 1u : 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (is_psx_bios_vector(instruction_pc)) {
        return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
    }

    const auto fetched = psx_bus_read_u32(runtime.bus, instruction_pc);
    if (fetched.reason != PsxBusAccessReason::ok) {
        return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
    }

    if ((fetched.value >> 26u) == 0x12u) {
        return step_psx_gte_transfer(runtime, fetched.value);
    }

    const auto stepped = step_psx_r3000a(runtime.cpu, fetched.value, runtime.bus);
    if (stepped.reason == PsxR3000aStepReason::exception &&
        stepped.exception_code == PsxR3000aExceptionCode::syscall &&
        handle_psx_syscall_exception(runtime)) {
        return {PsxR3000aStepReason::ok, stepped.instruction_pc, stepped.instruction};
    }
    return stepped;
}

}