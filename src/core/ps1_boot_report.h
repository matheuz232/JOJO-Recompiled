#pragma once

#include "core/ps1_memory_bus.h"
#include "core/r3000a_diagnostics.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

enum class Ps1BootStopReason : std::uint8_t {
    none,
    execution_budget_exhausted,
    cpu_boundary,
    bios_call_unimplemented,
    bios_call_unknown,
    mmio_unimplemented,
    installed_media_missing,
    device_command_unimplemented,
    gpu_command_unimplemented,
    commercial_frame_presented,
    fatal_runtime_error,
};

struct Ps1BiosCallSummary {
    std::uint32_t pc{};
    std::uint32_t table_physical{};
    std::uint32_t selector{};
};

struct Ps1MmioSummary {
    std::uint32_t pc{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
};

struct Ps1CdromCommandSummary {
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
};

struct Ps1BootReport {
    std::uint64_t instructions_retired{};
    std::uint32_t last_pc{};
    std::optional<std::uint32_t> last_opcode{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
    std::uint64_t bios_call_count{};
    std::vector<Ps1BiosCallSummary> recent_bios_calls;
    std::vector<Ps1MmioSummary> recent_mmio;
    std::uint64_t interrupts_accepted{};
    std::uint64_t dma_transfer_count{};
    std::vector<Ps1CdromCommandSummary> recent_cdrom_commands;
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t vram_write_count{};
    std::uint64_t presented_frames{};
    std::optional<R3000aDiagnostic> cpu_diagnostic{};
    std::optional<Ps1UnsupportedAccess> unsupported_access{};
};

struct Ps1BootOptions {
    std::uint64_t instruction_budget{10000u};
};

} // namespace jojo
