#pragma once

#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

enum class Ps1Max3DependencyKind : std::uint8_t {
    bios_frontier,
    speculative_mmio,
    terminal_mmio,
};

enum class Ps1Max3EvidenceClass : std::uint8_t {
    strict,
    speculative,
};

enum class Ps1Max3Profile : std::uint8_t {
    strict,
    deep,
    omega,
};

enum class Ps1Max3CandidateSource : std::uint8_t {
    baseline_zero,
    baseline_one,
    baseline_all_ones,
    sign_bit,
    strict_observed,
    zero_test_class,
    mask_class,
    threshold_class,
    coverage_retained,
    diagnostic_no_effect,
};

enum class Ps1Max3Subsystem : std::uint8_t {
    bios,
    cpu,
    irq,
    cdrom,
    gpu,
    dma,
    timer,
    mmio,
    other,
};

enum class Ps1Max3PruneReason : std::uint8_t {
    none,
    exact_duplicate,
    dominated,
    exact_cycle,
    branch_depth,
    speculative_depth,
    frontier_budget,
    state_budget,
    queue_budget,
    descendant_budget,
    retired_budget,
};

enum class Ps1Max3DecisionKind : std::uint8_t {
    bios_fallback,
    mmio_read_fallback,
    mmio_write_no_effect,
};

enum class Ps1Max3FrontierKind : std::uint8_t {
    bios,
    mmio_read,
    terminal_mmio_write,
    device_command,
    gpu_command,
    cpu_boundary,
    diagnostic_stall,
    execution_budget,
    fatal_runtime_error,
    other_terminal,
};

enum class Ps1Max3ExpansionStop : std::uint8_t {
    none,
    branch_depth,
    speculative_depth,
    deduplicated,
    terminal_frontier,
};

enum class Ps1Max3TerminationReason : std::uint8_t {
    completed,
    node_limit,
    total_retired_limit,
    frontier_limit,
};

struct Ps1Max3Options {
    std::size_t max_nodes{5461u};
    std::size_t max_branch_depth{6u};
    std::uint64_t max_total_retired{1000000000ull};
    Ps1BootOptions segment_options{};
    bool deep_frontier_enabled{false};
    std::size_t max_unique_frontiers{32u};
    std::size_t max_speculative_depth{8u};
    Ps1Max3Profile profile{Ps1Max3Profile::strict};
    std::size_t max_candidates_per_read{3u};
    std::size_t max_unique_states{65536u};
    std::size_t max_queued_states{16384u};
    std::size_t max_descendants_per_frontier{512u};
    std::size_t max_cycle_repeats{1u};
    std::uint64_t max_serialized_diagnostic_bytes{64ull * 1024ull * 1024ull};
};

struct Ps1Max3Decision {
    std::uint32_t table{};
    std::uint32_t selector{};
    Ps1BiosFallback fallback{Ps1BiosFallback::return_zero};
    Ps1Max3DecisionKind kind{Ps1Max3DecisionKind::bios_fallback};
    std::uint32_t address{};
    std::uint8_t width{};
    std::uint32_t value{};
    Ps1Max3CandidateSource candidate_source{Ps1Max3CandidateSource::baseline_zero};
};

struct Ps1Max3NodeSummary {
    std::size_t index{};
    std::optional<std::size_t> parent{};
    std::size_t depth{};
    std::optional<Ps1BiosFallback> fallback{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
    std::uint64_t segment_retired{};
    std::uint64_t cumulative_retired{};
    std::uint64_t state_hash{};
    bool deduplicated{};
    std::uint32_t frontier_table{};
    std::uint32_t frontier_selector{};
    std::uint64_t path_presented_frames{};
    std::uint64_t path_vram_write_count{};
    std::uint64_t path_gpu_gp0_command_count{};
    std::uint64_t path_gpu_gp1_command_count{};
    std::uint64_t path_cdrom_command_count{};
    std::uint64_t path_dma_transfer_count{};
    std::size_t path_dependency_count{};
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    std::size_t speculative_depth{};
    std::optional<Ps1Max3Decision> decision{};
    std::optional<std::size_t> frontier{};
    Ps1Max3ExpansionStop expansion_stop{Ps1Max3ExpansionStop::none};
    Ps1Max3PruneReason prune_reason{Ps1Max3PruneReason::none};
    std::size_t assumption_count{};
    std::size_t new_frontier_count{};
    std::size_t new_subsystem_coverage{};
    std::size_t new_instruction_coverage{};
    bool reached_progress_landmark{};
};

struct Ps1Max3Dependency {
    Ps1Max3DependencyKind kind{Ps1Max3DependencyKind::bios_frontier};
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
};

struct Ps1Max3Frontier {
    std::size_t index{};
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    Ps1Max3FrontierKind kind{Ps1Max3FrontierKind::other_terminal};
    std::uint32_t pc{};
    std::optional<std::uint32_t> opcode{};
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
    std::size_t first_node{};
    std::size_t occurrence_count{};
    std::optional<std::size_t> parent_frontier{};
    std::optional<Ps1Max3Decision> parent_decision{};
    std::vector<Ps1Max3Decision> assumption_chain;
    bool expandable{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
};

struct Ps1Max3SearchStats {
    std::size_t states_visited{};
    std::size_t states_deduplicated{};
    std::size_t states_dominated{};
    std::size_t cycles_cut{};
    std::size_t candidates_considered{};
    std::size_t candidates_expanded{};
    std::size_t candidates_pruned{};
    std::size_t queue_high_watermark{};
    std::size_t max_branch_depth{};
    std::size_t max_speculative_depth{};
    std::size_t budget_expansions{};
    std::uint64_t max_presented_frames{};
    std::uint64_t max_vram_write_count{};
    std::uint64_t max_gpu_gp0_command_count{};
    std::uint64_t max_gpu_gp1_command_count{};
    std::uint64_t max_cdrom_command_count{};
    std::uint64_t max_dma_transfer_count{};
    std::uint64_t max_interrupts_accepted{};
};

struct Ps1Max3FrontierCluster {
    std::size_t index{};
    Ps1Max3Subsystem subsystem{Ps1Max3Subsystem::other};
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::speculative};
    std::size_t occurrence_count{};
    std::size_t strict_occurrence_count{};
    std::size_t speculative_occurrence_count{};
    std::size_t callsite_count{};
    std::size_t descendant_count{};
    std::uint64_t priority_score{};
    std::vector<std::size_t> frontier_indices;
};

struct Ps1Max3Report {
    Ps1Max3Options options{};
    Ps1Max3TerminationReason termination_reason{Ps1Max3TerminationReason::completed};
    std::uint64_t total_retired{};
    std::vector<Ps1Max3NodeSummary> nodes;
    std::vector<Ps1Max3Dependency> dependencies;
    std::size_t best_node{};
    std::vector<Ps1Max3Decision> best_path;
    Ps1BootReport best_report{};
    std::vector<Ps1Max3Frontier> frontiers;
    Ps1Max3SearchStats search_stats{};
    std::vector<Ps1Max3FrontierCluster> frontier_clusters;
};

[[nodiscard]] inline bool ps1_max3_best_is_strict_authoritative(
    const Ps1Max3Report& report) noexcept {
    return !report.nodes.empty() &&
           report.best_node < report.nodes.size() &&
           report.nodes[report.best_node].evidence == Ps1Max3EvidenceClass::strict;
}

[[nodiscard]] Ps1Max3Options ps1_max3_options(Ps1Max3Profile profile) noexcept;
[[nodiscard]] Ps1Max3Options ps1_max3_local_evidence_options() noexcept;
[[nodiscard]] Result<Ps1Max3Report> explore_ps1_max3(
    const Ps1Executable& executable,
    const Ps1Max3Options& options);

} // namespace jojo
