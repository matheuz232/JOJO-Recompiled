#pragma once
#include "core/psx_bus.h"
#include "core/psx_exe.h"
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
    // The retail BIOS initializes its CD-ROM interrupt handlers before handing
    // control to the boot executable. _96_remove attempts to dequeue them but
    // is ineffective because the BIOS SysDeqIntRP path is broken.
    bool cdrom_irq_handlers_installed{true};
    // The runtime starts at the boot executable rather than executing the ROM
    // boot sequence. Materialize the small, game-observed SCPH-1001 C0 table
    // surface once, then leave any game-applied patches intact.
    bool c0_table_materialized{};
};

struct PsxRuntime {
    PsxBus bus{};
    PsxR3000aState cpu{};
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
    // Decoded from the user's SCPH1001.BIN. The retail kernel's ResetEntryInt
    // at RAM 00000F2Ch stores 00006CF4h as EntryInt and returns that pointer.
    // The 30h-byte jmp_buf at 00006CF4h contains ReturnFromException (00000F40h),
    // the exception stack pointer 000085D4h, then FP/S0-S7/GP all zero.
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

    // C(06h) is the retail exception-handler entry used by commercial BIOS
    // compatibility patches. Only the C0 slot and handler words actually
    // observed by SLUS_010.60 are materialized; unobserved table entries are
    // intentionally not fabricated.
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

[[nodiscard]] inline bool handle_psx_syscall_exception(PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t branch_delay_bit = 0x80000000u;
    constexpr std::uint32_t previous_interrupt_enable = 1u << 2u;
    constexpr std::uint32_t interrupt_mask_bit_2 = 1u << 10u;
    constexpr std::uint32_t critical_bits = previous_interrupt_enable | interrupt_mask_bit_2;

    // The retail kernel has additional bookkeeping for a syscall in a branch
    // delay slot. Do not claim support for that path until it is required and
    // tested from real media.
    if ((runtime.cpu.cop0.cause & branch_delay_bit) != 0u) return false;

    switch (runtime.cpu.gpr[4]) {
    case 0u: // SYS(00h) NoFunction
        break;
    case 1u: { // SYS(01h) EnterCriticalSection
        const bool both_were_enabled =
            (runtime.cpu.cop0.status & critical_bits) == critical_bits;
        runtime.cpu.cop0.status &= ~critical_bits;
        runtime.cpu.gpr[2] = both_were_enabled ? 1u : 0u;
        break;
    }
    case 2u: // SYS(02h) ExitCriticalSection
        runtime.cpu.cop0.status |= critical_bits;
        break;
    default:
        return false;
    }

    // BIOS ReturnFromException executes RFE after advancing past the syscall.
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
        // Retail BIOS behavior: _96_remove() is a void routine whose attempt
        // to remove the CD-ROM priority-0 handlers fails through SysDeqIntRP.
        // The installed-chain state therefore remains unchanged.
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

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x56u) {
        constexpr std::uint32_t c0_table_address = 0x00000674u;
        if (!materialize_scph1001_c0_patch_surface(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = c0_table_address;
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

    const auto stepped = step_psx_r3000a(runtime.cpu, fetched.value, runtime.bus);
    if (stepped.reason == PsxR3000aStepReason::exception &&
        stepped.exception_code == PsxR3000aExceptionCode::syscall &&
        handle_psx_syscall_exception(runtime)) {
        return {PsxR3000aStepReason::ok, stepped.instruction_pc, stepped.instruction};
    }
    return stepped;
}

}
