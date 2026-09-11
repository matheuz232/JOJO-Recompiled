#include "core/ps1_max3_explorer.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void test_profiles_have_explicit_deterministic_defaults() {
    const auto strict = jojo::ps1_max3_options(jojo::Ps1Max3Profile::strict);
    CHECK(strict.profile == jojo::Ps1Max3Profile::strict);
    CHECK(!strict.deep_frontier_enabled);

    const auto deep = jojo::ps1_max3_options(jojo::Ps1Max3Profile::deep);
    CHECK(deep.profile == jojo::Ps1Max3Profile::deep);
    CHECK(deep.deep_frontier_enabled);
    CHECK(deep.max_candidates_per_read >= 3u);

    const auto omega = jojo::ps1_max3_options(jojo::Ps1Max3Profile::omega);
    CHECK(omega.profile == jojo::Ps1Max3Profile::omega);
    CHECK(omega.deep_frontier_enabled);
    CHECK(omega.max_nodes == 16383u);
    CHECK(omega.max_branch_depth == 32u);
    CHECK(omega.max_speculative_depth == 24u);
    CHECK(omega.max_unique_frontiers == 96u);
    CHECK(omega.max_total_retired == 3000000000ull);
    CHECK(omega.max_candidates_per_read == 8u);
    CHECK(omega.max_unique_states == 65536u);
    CHECK(omega.max_queued_states == 16384u);
    CHECK(omega.max_descendants_per_frontier == 512u);
    CHECK(omega.max_cycle_repeats == 1u);
    CHECK(omega.max_serialized_diagnostic_bytes == 64ull * 1024ull * 1024ull);
}

static void test_public_omega_model_types_are_available() {
    jojo::Ps1Max3Decision decision{};
    decision.candidate_source = jojo::Ps1Max3CandidateSource::baseline_one;
    CHECK(decision.candidate_source == jojo::Ps1Max3CandidateSource::baseline_one);

    jojo::Ps1Max3NodeSummary node{};
    node.prune_reason = jojo::Ps1Max3PruneReason::exact_duplicate;
    node.assumption_count = 2u;
    CHECK(node.prune_reason == jojo::Ps1Max3PruneReason::exact_duplicate);
    CHECK(node.assumption_count == 2u);

    jojo::Ps1Max3Report report{};
    report.search_stats.states_visited = 7u;
    report.frontier_clusters.push_back(jojo::Ps1Max3FrontierCluster{});
    CHECK(report.search_stats.states_visited == 7u);
    CHECK(report.frontier_clusters.size() == 1u);

    CHECK(static_cast<std::uint8_t>(jojo::Ps1Max3Subsystem::bios) !=
          static_cast<std::uint8_t>(jojo::Ps1Max3Subsystem::gpu));
}

static void test_compatibility_wrapper_remains_deep_until_commercial_activation() {
    const auto options = jojo::ps1_max3_local_evidence_options();
    CHECK(options.profile == jojo::Ps1Max3Profile::deep);
    CHECK(options.deep_frontier_enabled);
}

int main() {
    test_profiles_have_explicit_deterministic_defaults();
    test_public_omega_model_types_are_available();
    test_compatibility_wrapper_remains_deep_until_commercial_activation();
    return failures ? 1 : 0;
}
