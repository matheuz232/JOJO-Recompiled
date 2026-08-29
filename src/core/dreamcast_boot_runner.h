#pragma once

#include "core/dreamcast_bus.h"
#include "core/dreamcast_memory.h"
#include "core/result.h"
#include "core/sh4_reference_executor.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace jojo {

enum class DreamcastBootStopReason {
    end_of_program,
    left_program,
    block_limit,
    unsupported_opcode,
    unmapped_bus_access,
};

struct DreamcastBootRunResult {
    DreamcastExecutableMemory memory;
    Sh4ReferenceState state;
    DreamcastBootStopReason stop_reason{DreamcastBootStopReason::end_of_program};
    std::size_t blocks_executed{};
    std::size_t operations_executed{};
    std::optional<std::uint32_t> unsupported_address;
    std::optional<std::uint16_t> unsupported_raw;
    std::optional<DreamcastBusFault> bus_fault;
};

[[nodiscard]] Result<DreamcastBootRunResult> run_dreamcast_boot_reference(
    const DreamcastBootProgram& program,
    Sh4ReferenceState initial_state = {},
    std::size_t max_blocks = 100000u);

}
