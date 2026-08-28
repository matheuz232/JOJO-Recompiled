#pragma once
#include "core/result.h"
#include "core/sh4_decoder.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

enum class Sh4BlockExit {
    end_of_stream,
    fallthrough,
    conditional_branch,
    direct_branch,
    direct_call,
    indirect_call,
    indirect_jump,
    return_subroutine,
    return_exception,
    unsupported_instruction,
};

struct Sh4BasicBlock {
    std::uint32_t start_address{};
    std::vector<Sh4Instruction> instructions;
    Sh4BlockExit exit{Sh4BlockExit::end_of_stream};
    std::optional<std::uint32_t> branch_target;
    std::optional<std::uint32_t> fallthrough_target;
};

struct Sh4ControlFlowGraph {
    std::uint32_t base_address{};
    std::uint32_t entry_address{};
    std::vector<Sh4BasicBlock> blocks;
    std::vector<std::uint32_t> direct_call_targets;
    std::vector<std::uint32_t> indirect_call_sites;
    std::vector<std::uint32_t> indirect_jump_sites;
    std::vector<std::uint32_t> unsupported_sites;
};

[[nodiscard]] Result<Sh4ControlFlowGraph> build_sh4_cfg(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t base_address,
    std::uint32_t entry_address);

}
