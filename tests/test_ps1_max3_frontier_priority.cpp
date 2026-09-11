#include "core/ps1_max3_frontier_priority.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <iostream>
#include <limits>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Max3Frontier mmio_frontier(std::size_t index,
                                            std::uint32_t pc,
                                            bool write,
                                            jojo::Ps1Max3EvidenceClass evidence,
                                            std::size_t occurrences) {
    jojo::Ps1Max3Frontier frontier{};
    frontier.index = index;
    frontier.kind = write ? jojo::Ps1Max3FrontierKind::terminal_mmio_write
                          : jojo::Ps1Max3FrontierKind::mmio_read;
    frontier.evidence = evidence;
    frontier.pc = pc;
    frontier.address = 0x1F801802u;
    frontier.width = 1u;
    frontier.write = write;
    frontier.occurrence_count = occurrences;
    return frontier;
}

static jojo::Ps1Max3Frontier bios_frontier(std::size_t index,
                                            std::uint32_t pc,
                                            std::uint32_t selector,
                                            jojo::Ps1Max3EvidenceClass evidence,
                                            std::size_t occurrences) {
    jojo::Ps1Max3Frontier frontier{};
    frontier.index = index;
    frontier.kind = jojo::Ps1Max3FrontierKind::bios;
    frontier.evidence = evidence;
    frontier.pc = pc;
    frontier.table = 0xA0u;
    frontier.selector = selector;
    frontier.occurrence_count = occurrences;
    return frontier;
}

static const jojo::Ps1Max3FrontierCluster* cluster_with(
    const std::vector<jojo::Ps1Max3FrontierCluster>& clusters,
    std::size_t frontier_index) {
    const auto it = std::find_if(clusters.begin(), clusters.end(), [frontier_index](const auto& cluster) {
        return std::find(cluster.frontier_indices.begin(), cluster.frontier_indices.end(), frontier_index) !=
               cluster.frontier_indices.end();
    });
    return it == clusters.end() ? nullptr : &*it;
}

static void test_mmio_root_causes_cluster_across_callsites_but_not_direction() {
    jojo::Ps1Max3Report report{};
    report.frontiers.push_back(mmio_frontier(0u, 0x80010000u, false, jojo::Ps1Max3EvidenceClass::strict, 3u));
    report.frontiers.push_back(mmio_frontier(1u, 0x80020000u, false, jojo::Ps1Max3EvidenceClass::speculative, 2u));
    report.frontiers.push_back(mmio_frontier(2u, 0x80030000u, true, jojo::Ps1Max3EvidenceClass::strict, 7u));

    const auto clusters = jojo::cluster_and_rank_ps1_max3_frontiers(report);
    const auto* read = cluster_with(clusters, 0u);
    const auto* read_second = cluster_with(clusters, 1u);
    const auto* write = cluster_with(clusters, 2u);
    CHECK(read != nullptr);
    CHECK(read == read_second);
    CHECK(write != nullptr);
    CHECK(write != read);
    if (read) {
        CHECK(read->subsystem == jojo::Ps1Max3Subsystem::cdrom);
        CHECK(read->evidence == jojo::Ps1Max3EvidenceClass::strict);
        CHECK(read->occurrence_count == 5u);
        CHECK(read->strict_occurrence_count == 3u);
        CHECK(read->speculative_occurrence_count == 2u);
        CHECK(read->callsite_count == 2u);
    }
}

static void test_bios_selector_clusters_across_callsites() {
    jojo::Ps1Max3Report report{};
    report.frontiers.push_back(bios_frontier(0u, 0x80011110u, 0x33u, jojo::Ps1Max3EvidenceClass::strict, 1u));
    report.frontiers.push_back(bios_frontier(1u, 0x80022220u, 0x33u, jojo::Ps1Max3EvidenceClass::speculative, 4u));
    report.frontiers.push_back(bios_frontier(2u, 0x80033330u, 0x34u, jojo::Ps1Max3EvidenceClass::strict, 1u));

    const auto clusters = jojo::cluster_and_rank_ps1_max3_frontiers(report);
    CHECK(cluster_with(clusters, 0u) == cluster_with(clusters, 1u));
    CHECK(cluster_with(clusters, 0u) != cluster_with(clusters, 2u));
}

static void test_strict_evidence_and_unlock_value_rank_deterministically() {
    jojo::Ps1Max3Report report{};
    report.frontiers.push_back(mmio_frontier(0u, 0x80010000u, false, jojo::Ps1Max3EvidenceClass::strict, 1u));
    report.frontiers.push_back(mmio_frontier(1u, 0x80020000u, true, jojo::Ps1Max3EvidenceClass::speculative, 50u));
    report.frontiers.push_back(bios_frontier(2u, 0x80030000u, 0x44u, jojo::Ps1Max3EvidenceClass::strict, 1u));
    report.frontiers.push_back(bios_frontier(3u, 0x80031000u, 0x44u, jojo::Ps1Max3EvidenceClass::strict, 2u));
    report.frontiers.push_back(bios_frontier(4u, 0x80040000u, 0x55u, jojo::Ps1Max3EvidenceClass::strict, 1u));

    report.frontiers[3].parent_frontier = 2u;
    report.frontiers[4].parent_frontier = 2u;

    jojo::Ps1Max3NodeSummary progress{};
    progress.frontier = 2u;
    progress.path_vram_write_count = 1u;
    progress.path_gpu_gp0_command_count = 3u;
    report.nodes.push_back(progress);

    const auto first = jojo::cluster_and_rank_ps1_max3_frontiers(report);
    const auto second = jojo::cluster_and_rank_ps1_max3_frontiers(report);
    CHECK(first.size() == second.size());
    CHECK(!first.empty());
    for (std::size_t i = 0u; i < std::min(first.size(), second.size()); ++i) {
        CHECK(first[i].index == second[i].index);
        CHECK(first[i].priority_score == second[i].priority_score);
        CHECK(first[i].frontier_indices == second[i].frontier_indices);
    }

    const auto* strict_read = cluster_with(first, 0u);
    const auto* speculative_write = cluster_with(first, 1u);
    const auto* high_unlock_bios = cluster_with(first, 2u);
    const auto* low_unlock_bios = cluster_with(first, 4u);
    CHECK(strict_read && speculative_write && high_unlock_bios && low_unlock_bios);
    if (strict_read && speculative_write) CHECK(strict_read->priority_score > speculative_write->priority_score);
    if (high_unlock_bios && low_unlock_bios) {
        CHECK(high_unlock_bios->descendant_count > low_unlock_bios->descendant_count);
        CHECK(high_unlock_bios->priority_score > low_unlock_bios->priority_score);
    }
}

static jojo::Ps1Executable make_executable() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 9u, 0x33u),
        test_mips::i(0x09u, 0u, 10u, 0xA0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0u,
    };
    auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(parsed);
    return parsed ? std::move(parsed.value) : jojo::Ps1Executable{};
}

static void test_explorer_appends_ranked_clusters_to_report() {
    auto options = jojo::ps1_max3_options(jojo::Ps1Max3Profile::strict);
    options.max_nodes = 4u;
    options.max_branch_depth = 0u;
    options.max_total_retired = 1000u;
    options.segment_options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.segment_options.stagnation_instruction_limit = 16u;

    const auto explored = jojo::explore_ps1_max3(make_executable(), options);
    CHECK(explored);
    if (!explored) return;
    CHECK(explored.value.frontiers.size() == 1u);
    CHECK(explored.value.frontier_clusters.size() == 1u);
    if (!explored.value.frontier_clusters.empty()) {
        CHECK(explored.value.frontier_clusters[0].frontier_indices == std::vector<std::size_t>{0u});
        CHECK(explored.value.frontier_clusters[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
    }
}

int main() {
    test_mmio_root_causes_cluster_across_callsites_but_not_direction();
    test_bios_selector_clusters_across_callsites();
    test_strict_evidence_and_unlock_value_rank_deterministically();
    test_explorer_appends_ranked_clusters_to_report();
    return failures ? 1 : 0;
}
