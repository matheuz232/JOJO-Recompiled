#include "core/ps1_max3_budget.h"

#include <algorithm>
#include <limits>

namespace jojo {
namespace {

template <typename T>
T grow_dimension(T current, T ceiling) noexcept {
    if (current >= ceiling) return ceiling;
    if (current == T{}) return std::min<T>(T{1}, ceiling);
    if (current > std::numeric_limits<T>::max() / T{2}) return ceiling;
    return std::min<T>(static_cast<T>(current * T{2}), ceiling);
}

Ps1Max3BudgetStatus exhausted_status(const Ps1Max3BudgetEnvelope& envelope,
                                     const Ps1Max3BudgetUsage& usage) noexcept {
    if (usage.nodes >= envelope.max_nodes) return Ps1Max3BudgetStatus::node_limit;
    if (usage.unique_frontiers >= envelope.max_unique_frontiers) return Ps1Max3BudgetStatus::frontier_limit;
    if (usage.unique_states >= envelope.max_unique_states) return Ps1Max3BudgetStatus::state_limit;
    if (usage.queued_states >= envelope.max_queued_states) return Ps1Max3BudgetStatus::queue_limit;
    if (usage.total_retired >= envelope.max_total_retired) return Ps1Max3BudgetStatus::retired_limit;
    if (usage.branch_depth >= envelope.max_branch_depth) return Ps1Max3BudgetStatus::branch_depth_limit;
    if (usage.speculative_depth >= envelope.max_speculative_depth) return Ps1Max3BudgetStatus::speculative_depth_limit;
    if (usage.candidates_per_read >= envelope.max_candidates_per_read) return Ps1Max3BudgetStatus::candidate_limit;
    if (usage.descendants_for_frontier >= envelope.max_descendants_per_frontier) return Ps1Max3BudgetStatus::descendant_limit;
    if (usage.serialized_diagnostic_bytes >= envelope.max_serialized_diagnostic_bytes) {
        return Ps1Max3BudgetStatus::serialized_output_limit;
    }
    return Ps1Max3BudgetStatus::none;
}

bool exhausted_dimension_is_at_ceiling(Ps1Max3BudgetStatus status,
                                       const Ps1Max3BudgetEnvelope& current,
                                       const Ps1Max3BudgetEnvelope& ceiling) noexcept {
    switch (status) {
    case Ps1Max3BudgetStatus::node_limit:
        return current.max_nodes >= ceiling.max_nodes;
    case Ps1Max3BudgetStatus::frontier_limit:
        return current.max_unique_frontiers >= ceiling.max_unique_frontiers;
    case Ps1Max3BudgetStatus::state_limit:
        return current.max_unique_states >= ceiling.max_unique_states;
    case Ps1Max3BudgetStatus::queue_limit:
        return current.max_queued_states >= ceiling.max_queued_states;
    case Ps1Max3BudgetStatus::retired_limit:
        return current.max_total_retired >= ceiling.max_total_retired;
    case Ps1Max3BudgetStatus::branch_depth_limit:
        return current.max_branch_depth >= ceiling.max_branch_depth;
    case Ps1Max3BudgetStatus::speculative_depth_limit:
        return current.max_speculative_depth >= ceiling.max_speculative_depth;
    case Ps1Max3BudgetStatus::candidate_limit:
        return current.max_candidates_per_read >= ceiling.max_candidates_per_read;
    case Ps1Max3BudgetStatus::descendant_limit:
        return current.max_descendants_per_frontier >= ceiling.max_descendants_per_frontier;
    case Ps1Max3BudgetStatus::serialized_output_limit:
        return current.max_serialized_diagnostic_bytes >= ceiling.max_serialized_diagnostic_bytes;
    case Ps1Max3BudgetStatus::none:
        return false;
    }
    return false;
}

} // namespace

Ps1Max3BudgetEnvelope ps1_max3_budget_envelope(const Ps1Max3Options& options) noexcept {
    return Ps1Max3BudgetEnvelope{
        options.max_nodes,
        options.max_unique_frontiers,
        options.max_unique_states,
        options.max_queued_states,
        options.max_total_retired,
        options.max_branch_depth,
        options.max_speculative_depth,
        options.max_candidates_per_read,
        options.max_descendants_per_frontier,
        options.max_serialized_diagnostic_bytes,
    };
}

Ps1Max3BudgetDecision decide_ps1_max3_budget(
    const Ps1Max3BudgetEnvelope& current,
    const Ps1Max3BudgetEnvelope& ceiling,
    const Ps1Max3BudgetUsage& usage) noexcept {
    Ps1Max3BudgetDecision decision{current, Ps1Max3BudgetStatus::none, false};
    const auto exhausted = exhausted_status(current, usage);

    if (exhausted != Ps1Max3BudgetStatus::none &&
        exhausted_dimension_is_at_ceiling(exhausted, current, ceiling)) {
        decision.status = exhausted;
        return decision;
    }

    if (usage.recent_new_information < kPs1Max3BudgetExpansionInformationThreshold) {
        decision.status = exhausted;
        return decision;
    }

    decision.envelope.max_nodes = grow_dimension(current.max_nodes, ceiling.max_nodes);
    decision.envelope.max_unique_frontiers = grow_dimension(current.max_unique_frontiers, ceiling.max_unique_frontiers);
    decision.envelope.max_unique_states = grow_dimension(current.max_unique_states, ceiling.max_unique_states);
    decision.envelope.max_queued_states = grow_dimension(current.max_queued_states, ceiling.max_queued_states);
    decision.envelope.max_total_retired = grow_dimension(current.max_total_retired, ceiling.max_total_retired);
    decision.envelope.max_branch_depth = grow_dimension(current.max_branch_depth, ceiling.max_branch_depth);
    decision.envelope.max_speculative_depth = grow_dimension(current.max_speculative_depth, ceiling.max_speculative_depth);
    decision.envelope.max_candidates_per_read = grow_dimension(current.max_candidates_per_read, ceiling.max_candidates_per_read);
    decision.envelope.max_descendants_per_frontier = grow_dimension(current.max_descendants_per_frontier, ceiling.max_descendants_per_frontier);
    decision.envelope.max_serialized_diagnostic_bytes = grow_dimension(
        current.max_serialized_diagnostic_bytes, ceiling.max_serialized_diagnostic_bytes);

    decision.envelope.max_speculative_depth = std::min(
        decision.envelope.max_speculative_depth,
        decision.envelope.max_branch_depth);
    decision.expanded = !(decision.envelope == current);
    if (!decision.expanded) decision.status = exhausted;
    return decision;
}

} // namespace jojo
