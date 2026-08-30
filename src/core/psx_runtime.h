#pragma once
#include "core/psx_bus.h"
#include "core/psx_exe.h"
#include "core/psx_r3000a.h"
#include "core/psx_system_cnf.h"
#include "core/result.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace jojo {

struct PsxRuntime {
    PsxBus bus{};
    PsxR3000aState cpu{};
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

[[nodiscard]] inline PsxR3000aStepResult step_psx_runtime(PsxRuntime& runtime) noexcept {
    if (is_psx_bios_vector(runtime.cpu.pc)) {
        return {PsxR3000aStepReason::unsupported_instruction, runtime.cpu.pc, 0u};
    }

    const auto fetched = psx_bus_read_u32(runtime.bus, runtime.cpu.pc);
    if (fetched.reason != PsxBusAccessReason::ok) {
        return {PsxR3000aStepReason::memory_fault, runtime.cpu.pc, 0u};
    }
    return step_psx_r3000a(runtime.cpu, fetched.value, runtime.bus);
}

}
