#include "core/ps1_max3_explorer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

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

struct ExpandedBiosKey {
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint64_t state_hash{};

    friend bool operator<(const ExpandedBiosKey& lhs, const ExpandedBiosKey& rhs) noexcept {
        return std::tie(lhs.table, lhs.selector, lhs.state_hash) <
               std::tie(rhs.table, rhs.selector, rhs.state_hash);
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
    return result;
}

bool outranks(const Ps1Max3NodeSummary& candidate,
              const Ps1Max3NodeSummary& current) noexcept {
    if (candidate.path_presented_frames != current.path_presented_frames)
        return candidate.path_presented_frames > current.path_presented_frames;
    if (candidate.path_vram_write_count != current.path_vram_write_count)
        return candidate.path_vram_write_count > current.path_vram_write_count;
    if (candidate.path_gpu_gp0_command_count != current.path_gpu_gp0_command_count)
        return candidate.path_gpu_gp0_command_count > current.path_gpu_gp0_command_count;
    if (candidate.path_gpu_gp1_command_count != current.path_gpu_gp1_command_count)
        return candidate.path_gpu_gp1_command_count > current.path_gpu_gp1_command_count;
    if (candidate.path_cdrom_command_count != current.path_cdrom_command_count)
        return candidate.path_cdrom_command_count > current.path_cdrom_command_count;
    if (candidate.path_dma_transfer_count != current.path_dma_transfer_count)
        return candidate.path_dma_transfer_count > current.path_dma_transfer_count;
    if (candidate.path_dependency_count != current.path_dependency_count)
        return candidate.path_dependency_count > current.path_dependency_count;
    return candidate.cumulative_retired > current.cumulative_retired;
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

        visit(std::move(root.value), 0u, std::nullopt, std::nullopt,
              0u, PathMetrics{}, {}, {}, Ps1Max3EvidenceClass::strict,
              0u, std::nullopt, std::nullopt, {});
        return Result<Ps1Max3Report>::success(std::move(report_));
    }

private:
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
        const auto width = segment.unsupported_access
            ? segment.unsupported_access->width : 0u;
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
            ++report_.frontiers[existing->second].occurrence_count;
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
             options_.deep_frontier_enabled &&
             runtime.diagnostic_mmio_read_frontier().has_value());
        frontier.stop_reason = segment.stop_reason;

        const auto index = frontier.index;
        report_.frontiers.push_back(std::move(frontier));
        frontier_indices_.emplace(key, index);
        return index;
    }

    void visit(Ps1BootRuntime runtime,
               std::size_t depth,
               std::optional<std::size_t> parent,
               std::optional<Ps1BiosFallback> fallback,
               std::uint64_t cumulative_before,
               const PathMetrics& metrics_before,
               std::vector<Ps1Max3Decision> path,
               std::set<DependencyKey> path_dependencies,
               Ps1Max3EvidenceClass evidence,
               std::size_t speculative_depth,
               std::optional<Ps1Max3Decision> parent_decision,
               std::optional<std::size_t> parent_frontier,
               std::vector<Ps1Max3Decision> assumption_chain) {
        if (report_.termination_reason != Ps1Max3TerminationReason::completed) return;
        if (report_.nodes.size() >= options_.max_nodes) {
            report_.termination_reason = Ps1Max3TerminationReason::node_limit;
            return;
        }
        if (!report_.nodes.empty() && report_.total_retired >= options_.max_total_retired) {
            report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
            return;
        }

        const std::uint64_t remaining =
            options_.max_total_retired >= report_.total_retired
                ? options_.max_total_retired - report_.total_retired
                : 0u;
        auto segment_options = options_.segment_options;
        segment_options.instruction_budget =
            std::min(segment_options.instruction_budget, remaining);

        Ps1BootReport segment = runtime.run(segment_options);
        report_.total_retired += segment.instructions_retired;
        const auto metrics = accumulate_metrics(metrics_before, segment);

        for (const auto& mmio : segment.recent_mmio) {
            if (!mmio.speculative) continue;
            add_dependency(report_, global_dependencies_, path_dependencies,
                           Ps1Max3Dependency{
                               Ps1Max3DependencyKind::speculative_mmio,
                               0u,
                               0u,
                               mmio.address,
                               mmio.width,
                               mmio.write,
                           });
        }

        if (segment.unsupported_access) {
            const auto& access = *segment.unsupported_access;
            add_dependency(report_, global_dependencies_, path_dependencies,
                           Ps1Max3Dependency{
                               Ps1Max3DependencyKind::terminal_mmio,
                               0u,
                               0u,
                               access.physical_address,
                               access.width,
                               access.write,
                               access.value,
                           });
        }

        std::uint32_t frontier_table = 0u;
        std::uint32_t frontier_selector = 0u;
        bool has_bios_frontier = false;
        if (segment.stop_reason == Ps1BootStopReason::bios_call_unimplemented) {
            const auto physical = Ps1MemoryBus::guest_to_physical(runtime.cpu_state().pc);
            if (physical && is_bios_table(*physical)) {
                frontier_table = *physical;
                frontier_selector = runtime.cpu_state().gpr[9];
                has_bios_frontier = true;
                add_dependency(report_, global_dependencies_, path_dependencies,
                               Ps1Max3Dependency{
                                   Ps1Max3DependencyKind::bios_frontier,
                                   frontier_table,
                                   frontier_selector,
                                   0u,
                                   0u,
                                   false,
                               });
            }
        }

        const auto state_hash = runtime.diagnostic_state_hash();
        const auto node_index = report_.nodes.size();
        const auto frontier_index = register_frontier(
            segment, runtime, node_index, evidence, parent_frontier,
            parent_decision, assumption_chain, has_bios_frontier,
            frontier_table, frontier_selector, state_hash);

        Ps1Max3NodeSummary summary{};
        summary.index = node_index;
        summary.parent = parent;
        summary.depth = depth;
        summary.fallback = fallback;
        summary.stop_reason = segment.stop_reason;
        summary.segment_retired = segment.instructions_retired;
        summary.cumulative_retired = cumulative_before + segment.instructions_retired;
        summary.state_hash = state_hash;
        summary.frontier_table = frontier_table;
        summary.frontier_selector = frontier_selector;
        summary.evidence = evidence;
        summary.speculative_depth = speculative_depth;
        summary.decision = parent_decision;
        summary.frontier = frontier_index;
        summary.path_presented_frames = metrics.presented_frames;
        summary.path_vram_write_count = metrics.vram_write_count;
        summary.path_gpu_gp0_command_count = metrics.gpu_gp0_command_count;
        summary.path_gpu_gp1_command_count = metrics.gpu_gp1_command_count;
        summary.path_cdrom_command_count = metrics.cdrom_command_count;
        summary.path_dma_transfer_count = metrics.dma_transfer_count;
        summary.path_dependency_count = path_dependencies.size();

        bool can_expand = has_bios_frontier && depth < options_.max_branch_depth;
        if (!has_bios_frontier || !report_.frontiers[frontier_index].expandable) {
            summary.expansion_stop = Ps1Max3ExpansionStop::terminal_frontier;
        } else if (depth >= options_.max_branch_depth) {
            summary.expansion_stop = Ps1Max3ExpansionStop::branch_depth;
        }
        if (can_expand) {
            const ExpandedBiosKey key{frontier_table, frontier_selector, state_hash};
            if (!expanded_bios_frontiers_.insert(key).second) {
                summary.deduplicated = true;
                summary.expansion_stop = Ps1Max3ExpansionStop::deduplicated;
                can_expand = false;
            }
        }

        report_.nodes.push_back(summary);

        if (report_.nodes.size() == 1u ||
            outranks(report_.nodes[node_index], report_.nodes[report_.best_node])) {
            report_.best_node = node_index;
            report_.best_path = path;
            report_.best_report = segment;
        }

        if (report_.total_retired >= options_.max_total_retired) {
            report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
            return;
        }
        if (!can_expand) return;

        for (const auto policy : kFallbackOrder) {
            if (report_.nodes.size() >= options_.max_nodes) {
                report_.termination_reason = Ps1Max3TerminationReason::node_limit;
                return;
            }
            if (report_.total_retired >= options_.max_total_retired) {
                report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
                return;
            }

            auto child = runtime;
            if (!child.apply_diagnostic_bios_fallback(policy)) continue;
            Ps1Max3Decision decision{};
            decision.kind = Ps1Max3DecisionKind::bios_fallback;
            decision.table = frontier_table;
            decision.selector = frontier_selector;
            decision.fallback = policy;

            auto child_path = path;
            child_path.push_back(decision);
            auto child_chain = assumption_chain;
            child_chain.push_back(decision);
            visit(std::move(child), depth + 1u, node_index, policy,
                  summary.cumulative_retired, metrics,
                  std::move(child_path), path_dependencies,
                  Ps1Max3EvidenceClass::speculative, speculative_depth + 1u,
                  decision, frontier_index, std::move(child_chain));
            if (report_.termination_reason != Ps1Max3TerminationReason::completed) return;
        }
    }

    const Ps1Max3Options& options_;
    Ps1Max3Report report_{};
    std::set<DependencyKey> global_dependencies_;
    std::set<ExpandedBiosKey> expanded_bios_frontiers_;
    std::map<FrontierRecordKey, std::size_t> frontier_indices_;
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
