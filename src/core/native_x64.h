#pragma once

#include "core/result.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo {

[[nodiscard]] bool native_x64_supported() noexcept;

[[nodiscard]] Result<std::vector<std::uint8_t>> compile_native_x64_block(
    const Sh4IrBlock& block);

[[nodiscard]] Result<void> execute_native_x64_block(
    std::span<const std::uint8_t> code,
    Sh4ReferenceState& state);

}
