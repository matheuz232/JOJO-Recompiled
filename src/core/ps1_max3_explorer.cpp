#include "core/ps1_max3_explorer.h"

#include "core/ps1_max3_candidate_engine.h"
#include "core/ps1_max3_search_policy.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace jojo {
namespace {

constexpr std::array<Ps1BiosFallback, 4> kFallbackOrder{
    Ps1BiosFallback::return_zero,
    Ps1BiosFallback::return_one,
    Ps1BiosFallback::return_minus_one,
    Ps1BiosFallback::preserve_v0,
};

struct DependencyKey {
    Ps1Max3DependencyKind kind{};
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint8_t width{};
    bool write{};

    friend bool operator<(const DependencyKey& lhs, const DependencyKey& rhs) noexcept {
        return std::tie(lhs.kind, lhs.first, lhs.second, lhs.width, lhs.write) <
               std::tie(rhs.kind, rhs.first, rhs.second, rhs.width, rhs.write);
    }
};

struct ExpansionKey {
    Ps1Max3FrontierKind kind{};
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint8_t width{};
    std::uint64_t state_hash{};

    friend bool operator<(const ExpansionKey& lhs, const ExpansionKey& rhs) noexcept {
        return std::tie(lhs.kind, lhs.first, lhs.second, lhs.width, lhs.state_hash) <
               std::tie(rhs.kind, rhs.first, rhs.second, rhs.width, rhs.state_hash);
    }
};

struct FrontierRecordKey {
    Ps1Max3FrontierKind kind{};
    std::uint32_t pc{};
    bool has_opcode{};
    std::uint32_t opcode{};
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
    std::uint64_t state_hash{};

    friend bool operator<(const FrontierRecordKey& lhs,
                          const FrontierRecordKey& rhs) noexcept {
        return std::tie(lhs.kind, lhs.pc, lhs.has_opcode, lhs.opcode,
                        lhs.table, lhs.selector, lhs.address, lhs.width,
                        lhs.write, lhs.value, lhs.state_hash) <
               std::tie(rhs.kind, rhs.pc, rhs.has_opcode, rhs.opcode,
                        rhs.table, rhs.selector, rhs.address, rhs.width,
                        rhs.write, rhs.value, rhs.state_hash);
    }
};

struct PathMetrics {
    std::uint64_t presented_frames{};
    std::uint64_t vram_write_count{};
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t dma_transfer_count{};
    std::uint64_t interrupts_accepted{};
};

struct WorkItem {
    Ps1BootRuntime runtime{};
    std::size_t depth{};
    std::optional<std::size_t> parent{};
    std::optional<Ps1BiosFallback> fallback{};
    std::uint64_t cumulative_before{};
    PathMetrics metrics_before{};
    std::vector<Ps1Max3Decision> path;
    std::set<DependencyKey> path_dependencies;
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    std::size_t speculative_depth{};
    std::optional<Ps1Max3Decision> parent_decision{};
    std::optional<std::size_t> parent_frontier{};
    std::vector<Ps1Max3Decision> assumption_chain;
    std::vector<std::uint64_t> ancestor_state_hashes;
    std::uint64_t insertion_sequence{};
};

bool is_bios_table(std::uint32_t physical) noexcept {
    return physical == 0x000000A0u ||
           physical == 0x000000B0u ||
           physical == 0x000000C0u;
}

DependencyKey dependency_key(const Ps1Max3Dependency& dependency) noexcept {
    if (dependency.kind == Ps1Max3DependencyKind::bios_frontier) {
        return DependencyKey{dependency.kind, dependency.table, dependency.selector, 0u, false};
    }
    if (dependency.kind == Ps1Max3DependencyKind::terminal_mmio) {
        return DependencyKey{
            dependency.kind,
            dependency.address,
            dependency.value,
            dependency.width,
            dependency.write,
        };
    }
    return DependencyKey{
        dependency.kind,
        dependency.address,
        0u,
        dependency.width,
        dependency.write,
    };
}

void add_dependency(Ps1Max3Report& report,
                    std::set<DependencyKey>& global_dependencies,
                    std::set<DependencyKey>& path_dependencies,
                    const Ps1Max3Dependency& dependency) {
    const auto key = dependency_key(dependency);
    path_dependencies.insert(key);
    if (global_dependencies.insert(key).second) {
        report.dependencies.push_back(dependency);
    }
}

PathMetrics accumulate_metrics(const PathMetrics& prior,
                               const Ps1BootReport& segment) noexcept {
    PathMetrics result = prior;
    result.presented_frames += segment.presented_frames;
    result.vram_write_count += segment.vram_write_count;
    result.gpu_gp0_command_count += segment.gpu_gp0_command_count;
    result.gpu_gp1_command_count += segment.gpu_gp1_command_count;
    result.cdrom_command_count += segment.cdrom_command_count;
    result.dma_transfer_count += segment.dma_transfer_count;
    result.interrupts_accepted += segment.interrupts_accepted;
    return result;
}

Ps1Max3SearchScore search_score(const WorkItem& item) noexcept {
    return Ps1Max3SearchScore{
        item.runtime.diagnostic_state_hash(),
        item.metrics_before.presented_frames,
        item.metrics_before.vram_write_count,
        item.metrics_before.gpu_gp0_command_count,
        item.metrics_before.gpu_gp1_command_count,
        item.metrics_before.dma_transfer_count,
        item.metrics_before.cdrom_command_count,
        item.metrics_before.interrupts_accepted,
        item.path_dependencies.size(),
        0u,
        item.evidence,
        item.assumption_chain.size(),
        item.speculative_depth,
        item.cumulative_before,
        item.insertion_sequence,
    };
}

Ps1Max3SearchScore search_score(const Ps1Max3NodeSummary& node,
                                std::uint64_t insertion_sequence) noexcept {
    return Ps1Max3SearchScore{
        node.state_hash,
        node.path_presented_frames,
        node.path_vram_write_count,
        node.path_gpu_gp0_command_count,
        node.path_gpu_gp1_command_count,
        node.path_dma_transfer_count,
        node.path_cdrom_command_count,
        0u,
        node.path_dependency_count,
        node.new_subsystem_coverage + node.new_instruction_coverage,
        node.evidence,
        node.assumption_count,
        node.speculative_depth,
        node.cumulative_retired,
        insertion_sequence,
    };
}

Ps1Max3FrontierKind classify_frontier(const Ps1BootReport& segment,
                                      bool bios_frontier) noexcept {
    if (bios_frontier) return Ps1Max3FrontierKind::bios;
    switch (segment.stop_reason) {
        case Ps1BootStopReason::mmio_unimplemented:
            if (segment.unsupported_access && segment.unsupported_access->write) {
                return Ps1Max3FrontierKind::terminal_mmio_write;
            }
            return Ps1Max3FrontierKind::mmio_read;
        case Ps1BootStopReason::device_command_unimplemented:
            return Ps1Max3FrontierKind::device_command;
        case Ps1BootStopReason::gpu_command_unimplemented:
            return Ps1Max3FrontierKind::gpu_command;
        case Ps1BootStopReason::cpu_boundary:
            return Ps1Max3FrontierKind::cpu_boundary;
        case Ps1BootStopReason::diagnostic_stall:
            return Ps1Max3FrontierKind::diagnostic_stall;
        case Ps1BootStopReason::execution_budget_exhausted:
            return Ps1Max3FrontierKind::execution_budget;
        case Ps1BootStopReason::fatal_runtime_error:
            return Ps1Max3FrontierKind::fatal_runtime_error;
        default:
            return Ps1Max3FrontierKind::other_terminal;
    }
}

ExpansionKey expansion_key(const Ps1Max3Frontier& frontier,
                           std::uint64_t state_hash) noexcept {
    if (frontier.kind == Ps1Max3FrontierKind::bios) {
        return ExpansionKey{frontier.kind, frontier.table, frontier.selector, 0u, state_hash};
    }
    return ExpansionKey{frontier.kind, frontier.address, 0u, frontier.width, state_hash};
}

class Explorer {
public:
    explicit Explorer(const Ps1Max3Options& options) : options_(options) {
        report_.options = options;
    }

    Result<Ps1Max3Report> run(const Ps1Executable& executable) {
        if (options_.max_nodes == 0u) {
            return Result<Ps1Max3Report>::failure(
                ErrorCode::invalid_argument,
                "PS1 MAX3 max_nodes must be greater than zero");
        }

        auto root = Ps1BootRuntime::create(executable);
        if (!root) {
            return Result<Ps1Max3Report>::failure(root.error, root.detail);
        }

        WorkItem root_item{};
        root_item.runtime = std::move(root.value);
        root_item.insertion_sequence = next_insertion_sequence_++;
        queue_.push_back(std::move(root_item));
        report_.search_stats.queue_high_watermark = 1u;

        while (!queue_.empty() && report_.termination_reason == Ps1Max3TerminationReason::completed) {
            if (report_.nodes.size() >= options_.max_nodes) {
                report_.termination_reason = Ps1Max3TerminationReason::node_limit;
                break;
            }
            if (!report_.nodes.empty() && report_.total_retired >= options_.max_total_retired) {
                report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
                break;
            }
            auto item = pop_best_work();
            process(std::move(item));
        }

        return Result<Ps1Max3Report>::success(std::move(report_));
    }

private:
    WorkItem pop_best_work() {
        std::size_t best = 0u;
        auto best_score = search_score(queue_.front());
        for (std::size_t i = 1u; i < queue_.size(); ++i) {
            const auto score = search_score(queue_[i]);
            if (ps1_max3_search_outranks(score, best_score)) {
                best = i;
                best_score = score;
            }
        }
        WorkItem item = std::move(queue_[best]);
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(best));
        return item;
    }

    std::size_t register_frontier(const Ps1BootReport& segment,
                                  const Ps1BootRuntime& runtime,
                                  std::size_t node_index,
                                  Ps1Max3EvidenceClass evidence,
                                  std::optional<std::size_t> parent_frontier,
                                  std::optional<Ps1Max3Decision> parent_decision,
                                  const std::vector<Ps1Max3Decision>& assumption_chain,
                                  bool has_bios_frontier,
                                  std::uint32_t frontier_table,
                                  std::uint32_t frontier_selector,
                                  std::uint64_t state_hash) {
        const auto kind = classify_frontier(segment, has_bios_frontier);
        const auto pc = segment.cpu_diagnostic ? segment.cpu_diagnostic->pc : segment.last_pc;
        const bool has_opcode = segment.last_opcode.has_value();
        const auto opcode = segment.last_opcode.value_or(0u);
        const auto address = segment.unsupported_access
            ? segment.unsupported_access->physical_address : 0u;
        const auto width = static_cast<std::uint8_t>(segment.unsupported_access
            ? segment.unsupported_access->width : 0u);
        const auto write = segment.unsupported_access
            ? segment.unsupported_access->write : false;
        const auto value = segment.unsupported_access
            ? segment.unsupported_access->value : 0u;

        const FrontierRecordKey key{
            kind, pc, has_opcode, opcode, frontier_table, frontier_selector,
            address, width, write, value, state_hash,
        };
        const auto existing = frontier_indices_.find(key);
        if (existing != frontier_indices_.end()) {
            auto& prior = report_.frontiers[existing->second];
            ++prior.occurrence_count;
            if (evidence == Ps1Max3EvidenceClass::strict) prior.evidence = evidence;
            return existing->second;
        }

        Ps1Max3Frontier frontier{};
        frontier.index = report_.frontiers.size();
        frontier.evidence = evidence;
        frontier.kind = kind;
        frontier.pc = pc;
        if (has_opcode) frontier.opcode = opcode;
        frontier.table = frontier_table;
        frontier.selector = frontier_selector;
        frontier.address = address;
        frontier.width = width;
        frontier.write = write;
        frontier.value = value;
        frontier.first_node = node_index;
        frontier.occurrence_count = 1u;
        frontier.parent_frontier = parent_frontier;
        frontier.parent_decision = parent_decision;
        frontier.assumption_chain = assumption_chain;
        frontier.expandable = kind == Ps1Max3FrontierKind::bios ||
            (kind == Ps1Max3FrontierKind::mmio_read &&
             options_.profile != Ps1Max3Profile::strict &&
             options_.deep_frontier_enabled &&
             runtime.diagnostic_mmio_read_frontier().has_value());
        frontier.stop_reason = segment.stop_reason;

        const auto index = frontier.index;
        report_.frontiers.push_back(std::move(frontier));
        frontier_indices_.emplace(key, index);
        return index;
    }

    void update_search_stats(const Ps1Max3NodeSummary& summary,
                             const PathMetrics& metrics) noexcept {
        auto& stats = report_.search_stats;
        ++stats.states_visited;
        stats.max_branch_depth = std::max(stats.max_branch_depth, summary.depth);
        stats.max_speculative_depth = std::max(stats.max_speculative_depth, summary.speculative_depth);
        stats.max_presented_frames = std::max(stats.max_presented_frames, metrics.presented_frames);
        stats.max_vram_write_count = std::max(stats.max_vram_write_count, metrics.vram_write_count);
        stats.max_gpu_gp0_command_count = std::max(stats.max_gpu_gp0_command_count, metrics.gpu_gp0_command_count);
        stats.max_gpu_gp1_command_count = std::max(stats.max_gpu_gp1_command_count, metrics.gpu_gp1_command_count);
        stats.max_cdrom_command_count = std::max(stats.max_cdrom_command_count, metrics.cdrom_command_count);
        stats.max_dma_transfer_count = std::max(stats.max_dma_transfer_count, metrics.dma_transfer_count);
        stats.max_interrupts_accepted = std::max(stats.max_interrupts_accepted, metrics.interrupts_accepted);
    }

    std::vector<std::uint32_t> following_opcodes(const Ps1BootRuntime& runtime,
                                                  std::uint32_t pc) const {
        std::vector<std::uint32_t> result;
        result.reserve(8u);
        auto inspector = runtime;
        for (std::uint32_t i = 1u; i <= 8u; ++i) {
            const auto read = inspector.bus().read32(pc + i * 4u);
            if (read.status != R3000aBusStatus::ok) break;
            result.push_back(read.value);
        }
        return result;
    }

    bool enqueue(WorkItem child, std::size_t frontier_index) {
        if (queue_.size() >= options_.max_queued_states) {
            ++report_.search_stats.candidates_pruned;
            return false;
        }
        auto& descendants = descendants_per_frontier_[frontier_index];
        if (descendants >= options_.max_descendants_per_frontier) {
            ++report_.search_stats.candidates_pruned;
            return false;
        }
        ++descendants;
        child.insertion_sequence = next_insertion_sequence_++;
        queue_.push_back(std::move(child));
        report_.search_stats.queue_high_watermark = std::max(
            report_.search_stats.queue_high_watermark, queue_.size());
        return true;
    }

    void expand_bios(const WorkItem& item,
                     const Ps1Max3NodeSummary& summary,
                     const PathMetrics& metrics,
                     std::size_t frontier_index,
                     std::uint32_t frontier_table,
                     std::uint32_t frontier_selector,
                     std::uint64_t state_hash) {
        auto ancestors = item.ancestor_state_hashes;
        ancestors.push_back(state_hash);
        for (const auto policy : kFallbackOrder) {
            auto child_runtime = item.runtime;
            if (!child_runtime.apply_diagnostic_bios_fallback(policy)) continue;

            Ps1Max3Decision decision{};
            decision.kind = Ps1Max3DecisionKind::bios_fallback;
            decision.table = frontier_table;
            decision.selector = frontier_selector;
            decision.fallback = policy;

            WorkItem child{};
            child.runtime = std::move(child_runtime);
            child.depth = item.depth + 1u;
            child.parent = summary.index;
            child.fallback = policy;
            child.cumulative_before = summary.cumulative_retired;
            child.metrics_before = metrics;
            child.path = item.path;
            child.path.push_back(decision);
            child.path_dependencies = item.path_dependencies;
            child.evidence = Ps1Max3EvidenceClass::speculative;
            child.speculative_depth = item.speculative_depth + 1u;
            child.parent_decision = decision;
            child.parent_frontier = frontier_index;
            child.assumption_chain = item.assumption_chain;
            child.assumption_chain.push_back(decision);
            child.ancestor_state_hashes = ancestors;
            enqueue(std::move(child), frontier_index);
        }
    }

    void expand_mmio(const WorkItem& item,
                     const Ps1Max3NodeSummary& summary,
                     const PathMetrics& metrics,
                     std::size_t frontier_index,
                     std::uint64_t state_hash) {
        const auto& blocked = item.runtime.diagnostic_mmio_read_frontier();
        if (!blocked) return;

        Ps1Max3CandidateContext context{};
        context.profile = options_.profile;
        context.width = blocked->access.width;
        context.pc = blocked->pc;
        context.load_opcode = blocked->opcode;
        context.max_candidates = options_.max_candidates_per_read;
        if (options_.profile == Ps1Max3Profile::omega) {
            context.following_opcodes = following_opcodes(item.runtime, blocked->pc);
        }
        const auto candidates = generate_ps1_max3_read_candidates(context);
        report_.search_stats.candidates_considered += candidates.size();

        auto ancestors = item.ancestor_state_hashes;
        ancestors.push_back(state_hash);
        for (const auto& candidate : candidates) {
            auto child_runtime = item.runtime;
            if (!child_runtime.apply_diagnostic_mmio_read_fallback(candidate.value)) {
                ++report_.search_stats.candidates_pruned;
                continue;
            }

            Ps1Max3Decision decision{};
            decision.kind = Ps1Max3DecisionKind::mmio_read_fallback;
            decision.address = blocked->access.physical_address;
            decision.width = blocked->access.width;
            decision.value = candidate.value;
            decision.candidate_source = candidate.source;

            WorkItem child{};
            child.runtime = std::move(child_runtime);
            child.depth = item.depth + 1u;
            child.parent = summary.index;
            child.cumulative_before = summary.cumulative_retired;
            child.metrics_before = metrics;
            child.path = item.path;
            child.path.push_back(decision);
            child.path_dependencies = item.path_dependencies;
            child.evidence = Ps1Max3EvidenceClass::speculative;
            child.speculative_depth = item.speculative_depth + 1u;
            child.parent_decision = decision;
            child.parent_frontier = frontier_index;
            child.assumption_chain = item.assumption_chain;
            child.assumption_chain.push_back(decision);
            child.ancestor_state_hashes = ancestors;
            if (enqueue(std::move(child), frontier_index)) {
                ++report_.search_stats.candidates_expanded;
            }
        }
    }

    void process(WorkItem item) {
        if (report_.nodes.size() >= options_.max_nodes) {
            report_.termination_reason = Ps1Max3TerminationReason::node_limit;
            return;
        }
        if (!report_.nodes.empty() && report_.total_retired >= options_.max_total_retired) {
            report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
            return;
        }

        const std::uint64_t remaining = options_.max_total_retired >= report_.total_retired
            ? options_.max_total_retired - report_.total_retired : 0u;
        auto segment_options = options_.segment_options;
        segment_options.instruction_budget = std::min(segment_options.instruction_budget, remaining);

        Ps1BootReport segment = item.runtime.run(segment_options);
        report_.total_retired += segment.instructions_retired;
        const auto metrics = accumulate_metrics(item.metrics_before, segment);

        for (const auto& mmio : segment.recent_mmio) {
            if (!mmio.speculative) continue;
            add_dependency(report_, global_dependencies_, item.path_dependencies,
                           Ps1Max3Dependency{
                               Ps1Max3DependencyKind::speculative_mmio,
                               0u, 0u, mmio.address, mmio.width, mmio.write,
                           });
        }

        if (segment.unsupported_access) {
            const auto& access = *segment.unsupported_access;
            add_dependency(report_, global_dependencies_, item.path_dependencies,
                           Ps1Max3Dependency{
                               Ps1Max3DependencyKind::terminal_mmio,
                               0u, 0u, access.physical_address,
                               access.width, access.write, access.value,
                           });
        }

        std::uint32_t frontier_table = 0u;
        std::uint32_t frontier_selector = 0u;
        bool has_bios_frontier = false;
        if (segment.stop_reason == Ps1BootStopReason::bios_call_unimplemented) {
            const auto physical = Ps1MemoryBus::guest_to_physical(item.runtime.cpu_state().pc);
            if (physical && is_bios_table(*physical)) {
                frontier_table = *physical;
                frontier_selector = item.runtime.cpu_state().gpr[9];
                has_bios_frontier = true;
                add_dependency(report_, global_dependencies_, item.path_dependencies,
                               Ps1Max3Dependency{
                                   Ps1Max3DependencyKind::bios_frontier,
                                   frontier_table, frontier_selector, 0u, 0u, false,
                               });
            }
        }

        const auto state_hash = item.runtime.diagnostic_state_hash();
        const auto node_index = report_.nodes.size();
        const auto frontiers_before = report_.frontiers.size();
        const auto frontier_index = register_frontier(
            segment, item.runtime, node_index, item.evidence, item.parent_frontier,
            item.parent_decision, item.assumption_chain, has_bios_frontier,
            frontier_table, frontier_selector, state_hash);

        Ps1Max3NodeSummary summary{};
        summary.index = node_index;
        summary.parent = item.parent;
        summary.depth = item.depth;
        summary.fallback = item.fallback;
        summary.stop_reason = segment.stop_reason;
        summary.segment_retired = segment.instructions_retired;
        summary.cumulative_retired = item.cumulative_before + segment.instructions_retired;
        summary.state_hash = state_hash;
        summary.frontier_table = frontier_table;
        summary.frontier_selector = frontier_selector;
        summary.evidence = item.evidence;
        summary.speculative_depth = item.speculative_depth;
        summary.decision = item.parent_decision;
        summary.frontier = frontier_index;
        summary.path_presented_frames = metrics.presented_frames;
        summary.path_vram_write_count = metrics.vram_write_count;
        summary.path_gpu_gp0_command_count = metrics.gpu_gp0_command_count;
        summary.path_gpu_gp1_command_count = metrics.gpu_gp1_command_count;
        summary.path_cdrom_command_count = metrics.cdrom_command_count;
        summary.path_dma_transfer_count = metrics.dma_transfer_count;
        summary.path_dependency_count = item.path_dependencies.size();
        summary.assumption_count = item.assumption_chain.size();
        summary.new_frontier_count = report_.frontiers.size() > frontiers_before ? 1u : 0u;

        auto& frontier = report_.frontiers[frontier_index];
        bool can_expand = frontier.expandable;
        if (!can_expand) {
            summary.expansion_stop = Ps1Max3ExpansionStop::terminal_frontier;
        } else if (item.depth >= options_.max_branch_depth) {
            summary.expansion_stop = Ps1Max3ExpansionStop::branch_depth;
            summary.prune_reason = Ps1Max3PruneReason::branch_depth;
            can_expand = false;
        } else if (item.speculative_depth >= options_.max_speculative_depth) {
            summary.expansion_stop = Ps1Max3ExpansionStop::speculative_depth;
            summary.prune_reason = Ps1Max3PruneReason::speculative_depth;
            can_expand = false;
        } else if (ps1_max3_is_exact_cycle(item.ancestor_state_hashes,
                                           state_hash,
                                           options_.max_cycle_repeats)) {
            summary.expansion_stop = Ps1Max3ExpansionStop::deduplicated;
            summary.prune_reason = Ps1Max3PruneReason::exact_cycle;
            ++report_.search_stats.cycles_cut;
            can_expand = false;
        }

        const auto key = expansion_key(frontier, state_hash);
        if (can_expand && !expanded_frontiers_.insert(key).second) {
            summary.deduplicated = true;
            summary.expansion_stop = Ps1Max3ExpansionStop::deduplicated;
            summary.prune_reason = Ps1Max3PruneReason::exact_duplicate;
            ++report_.search_stats.states_deduplicated;
            can_expand = false;
        }

        const auto score = search_score(summary, item.insertion_sequence);
        const auto prior = best_state_scores_.find(state_hash);
        if (can_expand && prior != best_state_scores_.end() &&
            ps1_max3_state_dominates(prior->second, score)) {
            summary.deduplicated = true;
            summary.expansion_stop = Ps1Max3ExpansionStop::deduplicated;
            summary.prune_reason = Ps1Max3PruneReason::dominated;
            ++report_.search_stats.states_dominated;
            can_expand = false;
        } else if (prior == best_state_scores_.end() ||
                   ps1_max3_search_outranks(score, prior->second)) {
            best_state_scores_[state_hash] = score;
        }

        report_.nodes.push_back(summary);
        update_search_stats(report_.nodes.back(), metrics);

        if (report_.nodes.size() == 1u ||
            ps1_max3_search_outranks(
                search_score(report_.nodes[node_index], item.insertion_sequence),
                search_score(report_.nodes[report_.best_node], best_node_sequence_))) {
            report_.best_node = node_index;
            best_node_sequence_ = item.insertion_sequence;
            report_.best_path = item.path;
            report_.best_report = segment;
        }

        if (report_.frontiers.size() > options_.max_unique_frontiers) {
            report_.termination_reason = Ps1Max3TerminationReason::frontier_limit;
            return;
        }
        if (report_.total_retired >= options_.max_total_retired) {
            report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
            return;
        }
        if (!can_expand) return;

        if (frontier.kind == Ps1Max3FrontierKind::bios) {
            expand_bios(item, report_.nodes[node_index], metrics, frontier_index,
                        frontier_table, frontier_selector, state_hash);
            return;
        }
        if (frontier.kind == Ps1Max3FrontierKind::mmio_read) {
            expand_mmio(item, report_.nodes[node_index], metrics, frontier_index, state_hash);
        }
    }

    const Ps1Max3Options& options_;
    Ps1Max3Report report_{};
    std::vector<WorkItem> queue_;
    std::set<DependencyKey> global_dependencies_;
    std::set<ExpansionKey> expanded_frontiers_;
    std::map<FrontierRecordKey, std::size_t> frontier_indices_;
    std::map<std::uint64_t, Ps1Max3SearchScore> best_state_scores_;
    std::map<std::size_t, std::size_t> descendants_per_frontier_;
    std::uint64_t next_insertion_sequence_{};
    std::uint64_t best_node_sequence_{};
};

} // namespace

Ps1Max3Options ps1_max3_options(Ps1Max3Profile profile) noexcept {
    Ps1Max3Options options{};
    options.profile = profile;
    options.max_nodes = 5461u;
    options.max_branch_depth = 6u;
    options.max_total_retired = 1000000000ull;
    options.max_unique_frontiers = 32u;
    options.max_speculative_depth = 8u;
    options.max_candidates_per_read = 3u;
    options.max_unique_states = 65536u;
    options.max_queued_states = 16384u;
    options.max_descendants_per_frontier = 512u;
    options.max_cycle_repeats = 1u;
    options.max_serialized_diagnostic_bytes = 64ull * 1024ull * 1024ull;
    options.segment_options = Ps1BootOptions{
        std::numeric_limits<std::uint64_t>::max(),
        131072u,
        true,
        65536u,
        65536u,
        2000000u,
    };
    options.deep_frontier_enabled = profile != Ps1Max3Profile::strict;

    if (profile == Ps1Max3Profile::omega) {
        options.max_nodes = 16383u;
        options.max_branch_depth = 32u;
        options.max_total_retired = 3000000000ull;
        options.max_unique_frontiers = 96u;
        options.max_speculative_depth = 24u;
        options.max_candidates_per_read = 8u;
    }
    return options;
}

Ps1Max3Options ps1_max3_local_evidence_options() noexcept {
    return ps1_max3_options(Ps1Max3Profile::deep);
}

Result<Ps1Max3Report> explore_ps1_max3(
    const Ps1Executable& executable,
    const Ps1Max3Options& options) {
    Explorer explorer(options);
    return explorer.run(executable);
}

} // namespace jojo
