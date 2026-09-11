#include "core/ps1_max3_budget.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Max3BudgetEnvelope make_current() {
    jojo::Ps1Max3BudgetEnvelope value{};
    value.max_nodes = 100u;
    value.max_unique_frontiers = 10u;
    value.max_unique_states = 20u;
    value.max_queued_states = 8u;
    value.max_total_retired = 1000u;
    value.max_branch_depth = 8u;
    value.max_speculative_depth = 4u;
    value.max_candidates_per_read = 4u;
    value.max_descendants_per_frontier = 16u;
    value.max_serialized_diagnostic_bytes = 1024u;
    return value;
}

static jojo::Ps1Max3BudgetEnvelope make_ceiling() {
    jojo::Ps1Max3BudgetEnvelope value{};
    value.max_nodes = 150u;
    value.max_unique_frontiers = 15u;
    value.max_unique_states = 30u;
    value.max_queued_states = 16u;
    value.max_total_retired = 1500u;
    value.max_branch_depth = 12u;
    value.max_speculative_depth = 9u;
    value.max_candidates_per_read = 6u;
    value.max_descendants_per_frontier = 20u;
    value.max_serialized_diagnostic_bytes = 1536u;
    return value;
}

static void test_options_map_to_budget_envelope() {
    auto options = jojo::ps1_max3_options(jojo::Ps1Max3Profile::omega);
    const auto envelope = jojo::ps1_max3_budget_envelope(options);
    CHECK(envelope.max_nodes == options.max_nodes);
    CHECK(envelope.max_unique_frontiers == options.max_unique_frontiers);
    CHECK(envelope.max_unique_states == options.max_unique_states);
    CHECK(envelope.max_queued_states == options.max_queued_states);
    CHECK(envelope.max_total_retired == options.max_total_retired);
    CHECK(envelope.max_branch_depth == options.max_branch_depth);
    CHECK(envelope.max_speculative_depth == options.max_speculative_depth);
    CHECK(envelope.max_candidates_per_read == options.max_candidates_per_read);
    CHECK(envelope.max_descendants_per_frontier == options.max_descendants_per_frontier);
    CHECK(envelope.max_serialized_diagnostic_bytes == options.max_serialized_diagnostic_bytes);
}

static void test_no_new_information_does_not_expand() {
    const auto current = make_current();
    const auto ceiling = make_ceiling();
    jojo::Ps1Max3BudgetUsage usage{};
    usage.recent_new_information = jojo::kPs1Max3BudgetExpansionInformationThreshold - 1u;

    const auto decision = jojo::decide_ps1_max3_budget(current, ceiling, usage);
    CHECK(!decision.expanded);
    CHECK(decision.status == jojo::Ps1Max3BudgetStatus::none);
    CHECK(decision.envelope == current);
}

static void test_information_threshold_doubles_and_caps_deterministically() {
    const auto current = make_current();
    const auto ceiling = make_ceiling();
    jojo::Ps1Max3BudgetUsage usage{};
    usage.recent_new_information = jojo::kPs1Max3BudgetExpansionInformationThreshold;

    const auto decision = jojo::decide_ps1_max3_budget(current, ceiling, usage);
    CHECK(decision.expanded);
    CHECK(decision.status == jojo::Ps1Max3BudgetStatus::none);
    CHECK(decision.envelope.max_nodes == 150u);
    CHECK(decision.envelope.max_unique_frontiers == 15u);
    CHECK(decision.envelope.max_unique_states == 30u);
    CHECK(decision.envelope.max_queued_states == 16u);
    CHECK(decision.envelope.max_total_retired == 1500u);
    CHECK(decision.envelope.max_branch_depth == 12u);
    CHECK(decision.envelope.max_speculative_depth == 8u);
    CHECK(decision.envelope.max_candidates_per_read == 6u);
    CHECK(decision.envelope.max_descendants_per_frontier == 20u);
    CHECK(decision.envelope.max_serialized_diagnostic_bytes == 1536u);
    CHECK(decision.envelope.max_speculative_depth <= decision.envelope.max_branch_depth);
}

static void test_zero_dimensions_grow_to_one_without_overflow() {
    jojo::Ps1Max3BudgetEnvelope current{};
    jojo::Ps1Max3BudgetEnvelope ceiling{};
    ceiling.max_nodes = 1u;
    ceiling.max_unique_frontiers = 1u;
    ceiling.max_unique_states = 1u;
    ceiling.max_queued_states = 1u;
    ceiling.max_total_retired = 1u;
    ceiling.max_branch_depth = 1u;
    ceiling.max_speculative_depth = 1u;
    ceiling.max_candidates_per_read = 1u;
    ceiling.max_descendants_per_frontier = 1u;
    ceiling.max_serialized_diagnostic_bytes = 1u;
    jojo::Ps1Max3BudgetUsage usage{};
    usage.recent_new_information = jojo::kPs1Max3BudgetExpansionInformationThreshold;

    const auto decision = jojo::decide_ps1_max3_budget(current, ceiling, usage);
    CHECK(decision.expanded);
    CHECK(decision.envelope == ceiling);
    CHECK(decision.envelope.max_speculative_depth <= decision.envelope.max_branch_depth);
}

static void test_each_exhausted_resource_has_distinct_status() {
    const auto ceiling = make_current();

    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.nodes = ceiling.max_nodes;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::node_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.unique_frontiers = ceiling.max_unique_frontiers;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::frontier_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.unique_states = ceiling.max_unique_states;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::state_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.queued_states = ceiling.max_queued_states;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::queue_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.total_retired = ceiling.max_total_retired;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::retired_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.branch_depth = ceiling.max_branch_depth;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::branch_depth_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.speculative_depth = ceiling.max_speculative_depth;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::speculative_depth_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.candidates_per_read = ceiling.max_candidates_per_read;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::candidate_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.descendants_for_frontier = ceiling.max_descendants_per_frontier;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::descendant_limit);
    }
    {
        jojo::Ps1Max3BudgetUsage usage{}; usage.serialized_diagnostic_bytes = ceiling.max_serialized_diagnostic_bytes;
        CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::serialized_output_limit);
    }
}

static void test_status_precedence_is_stable() {
    const auto ceiling = make_current();
    jojo::Ps1Max3BudgetUsage usage{};
    usage.nodes = ceiling.max_nodes;
    usage.unique_frontiers = ceiling.max_unique_frontiers;
    usage.unique_states = ceiling.max_unique_states;
    usage.queued_states = ceiling.max_queued_states;
    usage.total_retired = ceiling.max_total_retired;
    usage.branch_depth = ceiling.max_branch_depth;
    usage.speculative_depth = ceiling.max_speculative_depth;
    usage.candidates_per_read = ceiling.max_candidates_per_read;
    usage.descendants_for_frontier = ceiling.max_descendants_per_frontier;
    usage.serialized_diagnostic_bytes = ceiling.max_serialized_diagnostic_bytes;
    CHECK(jojo::decide_ps1_max3_budget(ceiling, ceiling, usage).status == jojo::Ps1Max3BudgetStatus::node_limit);
}

int main() {
    test_options_map_to_budget_envelope();
    test_no_new_information_does_not_expand();
    test_information_threshold_doubles_and_caps_deterministically();
    test_zero_dimensions_grow_to_one_without_overflow();
    test_each_exhausted_resource_has_distinct_status();
    test_status_precedence_is_stable();
    return failures ? 1 : 0;
}
