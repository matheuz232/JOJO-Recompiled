#pragma once

#include "core/ps1_max3_explorer.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace jojo {

struct Ps1Max3SearchScore {
    std::uint64_t state_hash{};
    std::uint64_t presented_frames{};
    std::uint64_t vram_write_count{};
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t dma_transfer_count{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t interrupt_callback_progress{};
    std::size_t new_frontier_count{};
    std::size_t new_coverage_count{};
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    std::size_t assumption_count{};
    std::size_t speculative_depth{};
    std::uint64_t retired{};
    std::uint64_t insertion_sequence{};
};

[[nodiscard]] bool ps1_max3_search_outranks(
    const Ps1Max3SearchScore& candidate,
    const Ps1Max3SearchScore& current) noexcept;

[[nodiscard]] bool ps1_max3_state_dominates(
    const Ps1Max3SearchScore& incumbent,
    const Ps1Max3SearchScore& candidate) noexcept;

[[nodiscard]] bool ps1_max3_is_exact_cycle(
    std::span<const std::uint64_t> ancestor_state_hashes,
    std::uint64_t state_hash,
    std::size_t max_cycle_repeats) noexcept;

} // namespace jojo
