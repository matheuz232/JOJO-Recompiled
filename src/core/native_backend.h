#pragma once

#include "core/dreamcast_memory.h"
#include "core/result.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jojo {

enum class NativeHostOp : std::uint8_t {
    nop,
    clear_t,
    set_t,
    move_t,
    set_imm,
    add_imm,
    copy_reg,
    add_reg,
};

struct NativeCompiledOp {
    NativeHostOp op{NativeHostOp::nop};
    std::uint8_t dst_reg{0xFFu};
    std::uint8_t src_reg{0xFFu};
    std::int32_t imm{};
};

struct NativeCompiledBlock {
    std::uint32_t start_address{};
    std::vector<NativeCompiledOp> ops;
    Sh4IrExit exit{Sh4IrExit::end_of_stream};
    std::optional<std::uint32_t> branch_target;
    std::optional<std::uint32_t> fallthrough_target;
    bool uses_native_lowering{};
};

struct NativeBackend {
    std::uint32_t abi_version{};
    std::string program_hash;
    Sh4IrProgram ir;
    std::vector<NativeCompiledBlock> blocks;
    std::size_t native_block_count{};
    std::size_t fallback_block_count{};
};

struct NativeRuntime {
    NativeBackend backend;
    DreamcastExecutableMemory memory;
    Sh4ReferenceState cpu;
    std::uint64_t frame_index{};
    std::uint64_t state_hash{};
};

struct NativeFrameStep {
    std::size_t blocks_executed{};
    std::size_t operations_executed{};
    bool used_reference_fallback{};
    bool reached_end{};
};

struct NativeBackendCacheInfo {
    bool rebuilt{};
    std::uint32_t abi_version{};
    std::string program_hash;
    std::size_t block_count{};
    std::size_t operation_count{};
    std::filesystem::path manifest_path;
    std::filesystem::path plan_path;
};

[[nodiscard]] std::uint32_t native_backend_abi_version() noexcept;

[[nodiscard]] Result<NativeRuntime> create_native_runtime(
    const DreamcastBootProgram& program);

[[nodiscard]] Result<NativeFrameStep> step_native_frame(
    NativeRuntime& runtime,
    std::size_t max_blocks = 100000u);

[[nodiscard]] Result<NativeBackendCacheInfo> ensure_native_backend_cache(
    const DreamcastBootProgram& program,
    const std::filesystem::path& install_dir);

[[nodiscard]] Result<NativeBackend> load_native_backend_cache(
    const std::filesystem::path& plan_path);

}
