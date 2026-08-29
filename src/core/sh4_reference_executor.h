#pragma once

#include "core/result.h"
#include "core/sh4_ir.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace jojo {

struct Sh4ReferenceState {
    std::array<std::uint32_t, 16> r{};
    std::uint32_t pc{};
    std::uint32_t pr{};
    std::uint32_t gbr{};
    bool t{};
};

struct Sh4ReferenceMemoryView {
    std::uint32_t base_address{};
    std::span<std::uint8_t> bytes{};
};

enum class Sh4ReferenceStopReason {
    end_of_stream,
    left_program,
    block_limit,
};

struct Sh4ReferenceRunResult {
    Sh4ReferenceStopReason stop_reason{Sh4ReferenceStopReason::end_of_stream};
    std::size_t blocks_executed{};
    std::size_t operations_executed{};
};

[[nodiscard]] Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceMemoryView memory,
    std::size_t max_blocks);

}
