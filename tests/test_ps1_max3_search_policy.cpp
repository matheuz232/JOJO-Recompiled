#include "core/ps1_max3_search_policy.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

namespace {

jojo::Ps1Max3SearchScore baseline() {
    jojo::Ps1Max3SearchScore score{};
    score.state_hash = 0x12345678u;
    score.evidence = jojo::Ps1Max3EvidenceClass::strict;
    score.insertion_sequence = 10u;
    return score;
}

void test_priority_order_is_lexicographic_and_deterministic() {
    auto lower = baseline();
    auto higher = lower;

    higher.presented_frames = 1u;
    lower.vram_write_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.vram_write_count = 1u;
    lower.gpu_gp0_command_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.gpu_gp0_command_count = 1u;
    lower.gpu_gp1_command_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.gpu_gp1_command_count = 1u;
    lower.dma_transfer_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.dma_transfer_count = 1u;
    lower.cdrom_command_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.cdrom_command_count = 1u;
    lower.interrupt_callback_progress = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.interrupt_callback_progress = 1u;
    lower.new_frontier_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.new_frontier_count = 1u;
    lower.new_coverage_count = 100u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.new_coverage_count = 1u;
    lower.evidence = jojo::Ps1Max3EvidenceClass::speculative;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    lower.evidence = jojo::Ps1Max3EvidenceClass::speculative;
    higher.assumption_count = 1u;
    lower.assumption_count = 2u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.speculative_depth = 1u;
    lower.speculative_depth = 2u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.retired = 11u;
    lower.retired = 10u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));

    lower = baseline(); higher = lower;
    higher.insertion_sequence = 9u;
    lower.insertion_sequence = 10u;
    CHECK(jojo::ps1_max3_search_outranks(higher, lower));
    CHECK(!jojo::ps1_max3_search_outranks(lower, higher));
    CHECK(!jojo::ps1_max3_search_outranks(lower, lower));
}

void test_strict_evidence_beats_equivalent_speculative_path() {
    auto strict = baseline();
    auto speculative = strict;
    strict.evidence = jojo::Ps1Max3EvidenceClass::strict;
    speculative.evidence = jojo::Ps1Max3EvidenceClass::speculative;
    speculative.assumption_count = 0u;
    speculative.speculative_depth = 0u;
    CHECK(jojo::ps1_max3_search_outranks(strict, speculative));
    CHECK(!jojo::ps1_max3_search_outranks(speculative, strict));
}

void test_dominance_requires_same_state_and_no_better_progress() {
    auto incumbent = baseline();
    incumbent.presented_frames = 1u;
    incumbent.new_frontier_count = 2u;
    incumbent.assumption_count = 1u;
    incumbent.speculative_depth = 1u;

    auto candidate = incumbent;
    CHECK(jojo::ps1_max3_state_dominates(incumbent, candidate));

    candidate = incumbent;
    candidate.assumption_count = 2u;
    candidate.speculative_depth = 2u;
    CHECK(jojo::ps1_max3_state_dominates(incumbent, candidate));

    candidate = incumbent;
    candidate.presented_frames = 2u;
    candidate.assumption_count = 2u;
    CHECK(!jojo::ps1_max3_state_dominates(incumbent, candidate));

    candidate = incumbent;
    candidate.state_hash ^= 1u;
    candidate.assumption_count = 99u;
    CHECK(!jojo::ps1_max3_state_dominates(incumbent, candidate));

    candidate = incumbent;
    candidate.evidence = jojo::Ps1Max3EvidenceClass::strict;
    incumbent.evidence = jojo::Ps1Max3EvidenceClass::speculative;
    CHECK(!jojo::ps1_max3_state_dominates(incumbent, candidate));
}

void test_exact_cycle_counts_only_identical_ancestor_hashes() {
    const std::vector<std::uint64_t> ancestors{0x10u, 0x20u, 0x30u, 0x20u};
    CHECK(jojo::ps1_max3_is_exact_cycle(ancestors, 0x20u, 1u));
    CHECK(jojo::ps1_max3_is_exact_cycle(ancestors, 0x20u, 2u));
    CHECK(!jojo::ps1_max3_is_exact_cycle(ancestors, 0x20u, 3u));
    CHECK(!jojo::ps1_max3_is_exact_cycle(ancestors, 0x21u, 1u));
}

void test_no_near_cycle_equivalence_is_inferred() {
    const std::vector<std::uint64_t> ancestors{0xaaaaaaaaaaaaaaaaull};
    CHECK(!jojo::ps1_max3_is_exact_cycle(
        ancestors, 0xaaaaaaaaaaaaaaabull, 1u));
}

} // namespace

int main() {
    test_priority_order_is_lexicographic_and_deterministic();
    test_strict_evidence_beats_equivalent_speculative_path();
    test_dominance_requires_same_state_and_no_better_progress();
    test_exact_cycle_counts_only_identical_ancestor_hashes();
    test_no_near_cycle_equivalence_is_inferred();
    return failures ? 1 : 0;
}
