#include "core/ps1_max3_frontier_priority.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace jojo {
namespace {

struct ClusterKey {
    Ps1Max3FrontierKind kind{Ps1Max3FrontierKind::other_terminal};
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint8_t width{};
    bool write{};

    friend bool operator<(const ClusterKey& lhs, const ClusterKey& rhs) noexcept {
        return std::tie(lhs.kind, lhs.first, lhs.second, lhs.width, lhs.write) <
               std::tie(rhs.kind, rhs.first, rhs.second, rhs.width, rhs.write);
    }
};

ClusterKey cluster_key(const Ps1Max3Frontier& frontier) noexcept {
    if (frontier.kind == Ps1Max3FrontierKind::bios) {
        return ClusterKey{frontier.kind, frontier.table, frontier.selector, 0u, false};
    }
    return ClusterKey{frontier.kind, frontier.address, 0u, frontier.width, frontier.write};
}

Ps1Max3Subsystem classify_subsystem(const Ps1Max3Frontier& frontier) noexcept {
    if (frontier.kind == Ps1Max3FrontierKind::bios) return Ps1Max3Subsystem::bios;
    if (frontier.kind == Ps1Max3FrontierKind::gpu_command ||
        frontier.address == 0x1F801810u || frontier.address == 0x1F801814u) {
        return Ps1Max3Subsystem::gpu;
    }
    if (frontier.address >= 0x1F801800u && frontier.address <= 0x1F801803u) {
        return Ps1Max3Subsystem::cdrom;
    }
    if (frontier.address >= 0x1F801080u && frontier.address <= 0x1F8010FFu) {
        return Ps1Max3Subsystem::dma;
    }
    if (frontier.address == 0x1F801070u || frontier.address == 0x1F801074u) {
        return Ps1Max3Subsystem::irq;
    }
    if (frontier.address >= 0x1F801100u && frontier.address <= 0x1F80112Fu) {
        return Ps1Max3Subsystem::timer;
    }
    if (frontier.kind == Ps1Max3FrontierKind::cpu_boundary ||
        frontier.kind == Ps1Max3FrontierKind::diagnostic_stall ||
        frontier.kind == Ps1Max3FrontierKind::execution_budget ||
        frontier.kind == Ps1Max3FrontierKind::fatal_runtime_error) {
        return Ps1Max3Subsystem::cpu;
    }
    if (frontier.address != 0u || frontier.kind == Ps1Max3FrontierKind::mmio_read ||
        frontier.kind == Ps1Max3FrontierKind::terminal_mmio_write ||
        frontier.kind == Ps1Max3FrontierKind::device_command) {
        return Ps1Max3Subsystem::mmio;
    }
    return Ps1Max3Subsystem::other;
}

std::uint64_t saturating_add(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

std::uint64_t saturating_mul(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    if (lhs == 0u || rhs == 0u) return 0u;
    if (lhs > std::numeric_limits<std::uint64_t>::max() / rhs) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs * rhs;
}

bool belongs_to_cluster(const Ps1Max3FrontierCluster& cluster,
                        std::size_t frontier_index) {
    return std::find(cluster.frontier_indices.begin(), cluster.frontier_indices.end(), frontier_index) !=
           cluster.frontier_indices.end();
}

std::size_t descendant_count(const Ps1Max3Report& report,
                             const Ps1Max3FrontierCluster& cluster) {
    std::map<std::size_t, const Ps1Max3Frontier*> by_index;
    for (const auto& frontier : report.frontiers) by_index.emplace(frontier.index, &frontier);

    std::size_t count = 0u;
    for (const auto& frontier : report.frontiers) {
        if (belongs_to_cluster(cluster, frontier.index)) continue;
        auto parent = frontier.parent_frontier;
        std::set<std::size_t> visited;
        bool descendant = false;
        while (parent && visited.insert(*parent).second) {
            if (belongs_to_cluster(cluster, *parent)) {
                descendant = true;
                break;
            }
            const auto it = by_index.find(*parent);
            if (it == by_index.end()) break;
            parent = it->second->parent_frontier;
        }
        if (descendant) ++count;
    }
    return count;
}

std::uint64_t progress_score(const Ps1Max3Report& report,
                             const Ps1Max3FrontierCluster& cluster) noexcept {
    std::uint64_t best = 0u;
    for (const auto& node : report.nodes) {
        if (!node.frontier || !belongs_to_cluster(cluster, *node.frontier)) continue;
        std::uint64_t score = 0u;
        score = saturating_add(score, saturating_mul(node.path_presented_frames, 1000000000000ull));
        score = saturating_add(score, saturating_mul(node.path_vram_write_count, 10000000000ull));
        score = saturating_add(score, saturating_mul(node.path_gpu_gp0_command_count, 100000000ull));
        score = saturating_add(score, saturating_mul(node.path_gpu_gp1_command_count, 10000000ull));
        score = saturating_add(score, saturating_mul(node.path_dma_transfer_count, 1000000ull));
        score = saturating_add(score, saturating_mul(node.path_cdrom_command_count, 100000ull));
        best = std::max(best, score);
    }
    return best;
}

std::uint64_t priority_score(const Ps1Max3Report& report,
                             const Ps1Max3FrontierCluster& cluster) noexcept {
    std::uint64_t score = 0u;
    score = saturating_add(score, saturating_mul(cluster.descendant_count, 1000000000ull));
    score = saturating_add(score, saturating_mul(cluster.callsite_count, 10000000ull));
    score = saturating_add(score, saturating_mul(cluster.occurrence_count, 100000ull));
    score = saturating_add(score, progress_score(report, cluster));
    return score;
}

} // namespace

std::vector<Ps1Max3FrontierCluster>
cluster_and_rank_ps1_max3_frontiers(const Ps1Max3Report& report) {
    std::map<ClusterKey, std::vector<const Ps1Max3Frontier*>> grouped;
    for (const auto& frontier : report.frontiers) {
        grouped[cluster_key(frontier)].push_back(&frontier);
    }

    std::vector<Ps1Max3FrontierCluster> clusters;
    clusters.reserve(grouped.size());
    std::size_t next_index = 0u;
    for (const auto& [key, members] : grouped) {
        (void)key;
        Ps1Max3FrontierCluster cluster{};
        cluster.index = next_index++;
        cluster.evidence = Ps1Max3EvidenceClass::speculative;
        std::set<std::uint32_t> callsites;
        bool has_strict = false;
        for (const auto* frontier : members) {
            cluster.frontier_indices.push_back(frontier->index);
            cluster.occurrence_count += frontier->occurrence_count;
            callsites.insert(frontier->pc);
            if (frontier->evidence == Ps1Max3EvidenceClass::strict) {
                has_strict = true;
                cluster.strict_occurrence_count += frontier->occurrence_count;
            } else {
                cluster.speculative_occurrence_count += frontier->occurrence_count;
            }
        }
        cluster.evidence = has_strict ? Ps1Max3EvidenceClass::strict
                                      : Ps1Max3EvidenceClass::speculative;
        cluster.callsite_count = callsites.size();
        cluster.subsystem = members.empty() ? Ps1Max3Subsystem::other
                                            : classify_subsystem(*members.front());
        clusters.push_back(std::move(cluster));
    }

    for (auto& cluster : clusters) {
        cluster.descendant_count = descendant_count(report, cluster);
        cluster.priority_score = priority_score(report, cluster);
    }

    std::stable_sort(clusters.begin(), clusters.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.evidence != rhs.evidence) {
            return lhs.evidence == Ps1Max3EvidenceClass::strict;
        }
        if (lhs.priority_score != rhs.priority_score) {
            return lhs.priority_score > rhs.priority_score;
        }
        return lhs.index < rhs.index;
    });
    return clusters;
}

} // namespace jojo
