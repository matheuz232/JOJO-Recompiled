#pragma once

#include "core/ps1_max3_explorer.h"

#include <cstddef>
#include <cstdint>

namespace jojo {

enum class Ps1Max3BudgetStatus : std::uint8_t {
    none,
    node_limit,
    frontier_limit,
    state_limit,
    queue_limit,
    retired_limit,
    branch_depth_limit,
    speculative_depth_limit,
    candidate_limit,
    descendant_limit,
    serialized_output_limit,
};

struct Ps1Max3BudgetEnvelope {
    std::size_t max_nodes{};
    std::size_t max_unique_frontiers{};
    std::size_t max_unique_states{};
    std::size_t max_queued_states{};
    std::uint64_t max_total_retired{};
    std::size_t max_branch_depth{};
    std::size_t max_speculative_depth{};
    std::size_t max_candidates_per_read{};
    std::size_t max_descendants_per_frontier{};
    std::uint64_t max_serialized_diagnostic_bytes{};

    bool operator==(const Ps1Max3BudgetEnvelope&) const = default;
};

struct Ps1Max3BudgetUsage {
    std::size_t nodes{};
    std::size_t unique_frontiers{};
    std::size_t unique_states{};
    std::size_t queued_states{};
    std::uint64_t total_retired{};
    std::size_t branch_depth{};
    std::size_t speculative_depth{};
    std::size_t candidates_per_read{};
    std::size_t descendants_for_frontier{};
    std::uint64_t serialized_diagnostic_bytes{};
    std::size_t recent_new_information{};
};

struct Ps1Max3BudgetDecision {
    Ps1Max3BudgetEnvelope envelope{};
    Ps1Max3BudgetStatus status{Ps1Max3BudgetStatus::none};
    bool expanded{};
};

inline constexpr std::size_t kPs1Max3BudgetExpansionInformationThreshold = 4u;

[[nodiscard]] Ps1Max3BudgetEnvelope ps1_max3_budget_envelope(
    const Ps1Max3Options& options) noexcept;

[[nodiscard]] Ps1Max3BudgetDecision decide_ps1_max3_budget(
    const Ps1Max3BudgetEnvelope& current,
    const Ps1Max3BudgetEnvelope& ceiling,
    const Ps1Max3BudgetUsage& usage) noexcept;

} // namespace jojo
