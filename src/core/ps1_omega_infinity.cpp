#include "core/ps1_omega_infinity.h"

#include "core/ps1_boot_report_io.h"
#include "core/ps1_max3_candidate_engine.h"
#include "core/ps1_max3_explorer.h"
#include "core/ps1_max3_search_policy.h"
#include "core/ps1_omega_coverage.h"
#include "core/ps1_omega_scheduler.h"
#include "core/ps1_omega_session_io.h"
#include "core/ps1_omega_snapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace jojo {
namespace {

constexpr std::array<Ps1BiosFallback, 4> kBiosFallbacks{
    Ps1BiosFallback::return_zero,
    Ps1BiosFallback::return_one,
    Ps1BiosFallback::return_minus_one,
    Ps1BiosFallback::preserve_v0,
};

struct Metrics {
    std::uint64_t presented_frames{};
    std::uint64_t vram_write_count{};
    std::uint64_t gp0{};
    std::uint64_t gp1{};
    std::uint64_t dma{};
    std::uint64_t cdrom{};
    std::uint64_t interrupts{};
};

struct WorkItem {
    Ps1BootRuntime runtime{};
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    std::size_t depth{};
    std::size_t speculative_depth{};
    std::uint64_t cumulative_retired{};
    std::uint64_t tail_retired{};
    std::uint64_t insertion_sequence{};
    Metrics metrics{};
    std::vector<Ps1Max3Decision> path;
    std::vector<Ps1Max3Decision> assumptions;
};

Metrics accumulate(Metrics metrics, const Ps1BootReport& report) noexcept {
    metrics.presented_frames += report.presented_frames;
    metrics.vram_write_count += report.vram_write_count;
    metrics.gp0 += report.gpu_gp0_command_count;
    metrics.gp1 += report.gpu_gp1_command_count;
    metrics.dma += report.dma_transfer_count;
    metrics.cdrom += report.cdrom_command_count;
    metrics.interrupts += report.interrupts_accepted;
    return metrics;
}

Ps1Max3SearchScore score(const WorkItem& item) noexcept {
    return Ps1Max3SearchScore{
        item.runtime.diagnostic_state_hash(),
        item.metrics.presented_frames,
        item.metrics.vram_write_count,
        item.metrics.gp0,
        item.metrics.gp1,
        item.metrics.dma,
        item.metrics.cdrom,
        item.metrics.interrupts,
        0u,
        0u,
        item.evidence,
        item.assumptions.size(),
        item.speculative_depth,
        item.cumulative_retired,
        item.insertion_sequence,
    };
}

WorkItem pop_best(std::vector<WorkItem>& queue) {
    std::size_t best{};
    auto best_score = score(queue.front());
    for (std::size_t i = 1u; i < queue.size(); ++i) {
        const auto candidate = score(queue[i]);
        if (ps1_max3_search_outranks(candidate, best_score)) {
            best = i;
            best_score = candidate;
        }
    }
    WorkItem result = std::move(queue[best]);
    queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(best));
    return result;
}

std::vector<std::uint32_t> following_opcodes(const Ps1BootRuntime& runtime,
                                              std::uint32_t pc) {
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

Ps1BootOptions segment_options(const Ps1OmegaInfinityOptions& options,
                               std::uint64_t budget) {
    Ps1BootOptions result{};
    result.instruction_budget = budget;
    result.trace_capacity = options.hot_trace_capacity;
    result.diagnostic_mmio_probe = false;
    result.mmio_event_capacity = 65536u;
    result.bios_event_capacity = 65536u;
    result.stagnation_instruction_limit = budget > 2000000u ? 2000000u : 0u;
    return result;
}

Ps1BootOptions replay_options(std::uint64_t budget) {
    Ps1BootOptions result{};
    result.instruction_budget = budget;
    result.trace_capacity = 0u;
    result.diagnostic_mmio_probe = false;
    result.mmio_event_capacity = 0u;
    result.bios_event_capacity = 0u;
    result.stagnation_instruction_limit = 0u;
    return result;
}

bool decision_matches_and_apply(Ps1BootRuntime& runtime,
                                const Ps1BootReport& report,
                                const Ps1Max3Decision& decision) {
    switch (decision.kind) {
    case Ps1Max3DecisionKind::bios_fallback: {
        const auto physical = Ps1MemoryBus::guest_to_physical(runtime.cpu_state().pc);
        if (!physical || *physical != decision.table || runtime.cpu_state().gpr[9] != decision.selector) {
            return false;
        }
        return runtime.apply_diagnostic_bios_fallback(decision.fallback);
    }
    case Ps1Max3DecisionKind::mmio_read_fallback:
        if (report.stop_reason != Ps1BootStopReason::mmio_unimplemented ||
            !report.unsupported_access || report.unsupported_access->write ||
            report.unsupported_access->physical_address != decision.address ||
            report.unsupported_access->width != decision.width) {
            return false;
        }
        return runtime.apply_diagnostic_mmio_read_fallback(decision.value);
    case Ps1Max3DecisionKind::mmio_write_no_effect:
        if (report.stop_reason != Ps1BootStopReason::mmio_unimplemented ||
            !report.unsupported_access || !report.unsupported_access->write ||
            report.unsupported_access->physical_address != decision.address ||
            report.unsupported_access->width != decision.width ||
            report.unsupported_access->value != decision.value) {
            return false;
        }
        return runtime.apply_diagnostic_mmio_write_no_effect(report);
    }
    return false;
}

std::optional<WorkItem> replay_descriptor(const Ps1Executable& executable,
                                          const Ps1OmegaReplayDescriptor& descriptor,
                                          std::uint64_t quantum) {
    auto created = Ps1BootRuntime::create(executable);
    if (!created) return std::nullopt;
    Ps1BootRuntime runtime = std::move(created.value);
    std::uint64_t replayed{};

    for (const auto& decision : descriptor.path) {
        Ps1BootReport frontier{};
        for (;;) {
            const auto cap = descriptor.cumulative_retired > replayed
                ? descriptor.cumulative_retired - replayed : 0u;
            if (cap == 0u) return std::nullopt;
            frontier = runtime.run(replay_options(std::min(quantum, cap)));
            replayed += frontier.instructions_retired;
            if (frontier.stop_reason == Ps1BootStopReason::execution_budget_exhausted) continue;
            break;
        }
        if (!decision_matches_and_apply(runtime, frontier, decision)) return std::nullopt;
    }

    std::uint64_t tail_done{};
    while (tail_done < descriptor.tail_retired) {
        const auto budget = std::min(quantum, descriptor.tail_retired - tail_done);
        const auto report = runtime.run(replay_options(budget));
        replayed += report.instructions_retired;
        tail_done += report.instructions_retired;
        if (report.stop_reason != Ps1BootStopReason::execution_budget_exhausted ||
            report.instructions_retired != budget) {
            return std::nullopt;
        }
    }

    if (replayed != descriptor.cumulative_retired ||
        runtime.diagnostic_state_hash() != descriptor.expected_state_hash) {
        return std::nullopt;
    }

    WorkItem item{};
    item.runtime = std::move(runtime);
    item.evidence = descriptor.evidence;
    item.depth = descriptor.path.size();
    item.speculative_depth = static_cast<std::size_t>(descriptor.speculative_depth);
    item.cumulative_retired = descriptor.cumulative_retired;
    item.tail_retired = descriptor.tail_retired;
    item.insertion_sequence = descriptor.insertion_sequence;
    item.metrics = Metrics{
        descriptor.presented_frames,
        descriptor.vram_write_count,
        descriptor.gpu_gp0_command_count,
        descriptor.gpu_gp1_command_count,
        descriptor.dma_transfer_count,
        descriptor.cdrom_command_count,
        descriptor.interrupt_callback_progress,
    };
    item.path = descriptor.path;
    item.assumptions = descriptor.assumption_chain;
    return item;
}

Ps1OmegaReplayDescriptor describe(const WorkItem& item) {
    Ps1OmegaReplayDescriptor descriptor{};
    descriptor.evidence = item.evidence;
    descriptor.expected_state_hash = item.runtime.diagnostic_state_hash();
    descriptor.insertion_sequence = item.insertion_sequence;
    descriptor.cumulative_retired = item.cumulative_retired;
    descriptor.tail_retired = item.tail_retired;
    descriptor.presented_frames = item.metrics.presented_frames;
    descriptor.vram_write_count = item.metrics.vram_write_count;
    descriptor.gpu_gp0_command_count = item.metrics.gp0;
    descriptor.gpu_gp1_command_count = item.metrics.gp1;
    descriptor.dma_transfer_count = item.metrics.dma;
    descriptor.cdrom_command_count = item.metrics.cdrom;
    descriptor.interrupt_callback_progress = item.metrics.interrupts;
    descriptor.speculative_depth = item.speculative_depth;
    descriptor.path = item.path;
    descriptor.assumption_chain = item.assumptions;
    return descriptor;
}

std::optional<std::vector<std::uint8_t>> read_chunk_payload(
    const std::filesystem::path& root,
    const Ps1OmegaChunkRecord& record) {
    const auto path = root / record.relative_path;
    const auto digest = sha256_file(path);
    if (!digest || sha256_hex(digest.value) != record.sha256) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() != kPs1OmegaChunkHeaderBytes + record.payload_bytes) return std::nullopt;
    return std::vector<std::uint8_t>(
        bytes.begin() + static_cast<std::ptrdiff_t>(kPs1OmegaChunkHeaderBytes), bytes.end());
}

std::optional<Ps1OmegaResumeState> load_latest_resume(
    const std::filesystem::path& root,
    const Ps1OmegaSessionManifest& manifest) {
    for (auto it = manifest.chunks.rbegin(); it != manifest.chunks.rend(); ++it) {
        if (it->category != Ps1OmegaEvidenceCategory::search) continue;
        auto payload = read_chunk_payload(root, *it);
        if (!payload) return std::nullopt;
        return decode_ps1_omega_resume_state(*payload);
    }
    return std::nullopt;
}

std::vector<std::uint8_t> text_bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

void observe_report(Ps1OmegaCoverage& coverage,
                    const Ps1BootReport& report,
                    Ps1Max3EvidenceClass evidence,
                    std::uint64_t cumulative) {
    std::optional<std::uint32_t> prior;
    for (const auto& trace : report.recent_trace) {
        coverage.observe_pc(trace.pc);
        if (prior) coverage.observe_edge(*prior, trace.pc);
        prior = trace.pc;
    }
    for (const auto& mmio : report.recent_mmio) {
        coverage.observe_mmio(mmio.address, mmio.width, mmio.write);
    }
    if (report.gpu_gp0_command_count != 0u) {
        coverage.frame_first().record(Ps1OmegaFrameFirstLandmark::first_gp0, evidence, cumulative);
    }
    if (report.dma_transfer_count != 0u) {
        coverage.frame_first().record(Ps1OmegaFrameFirstLandmark::first_dma_to_gpu, evidence, cumulative);
    }
    if (report.vram_write_count != 0u) {
        coverage.frame_first().record(Ps1OmegaFrameFirstLandmark::first_vram_write, evidence, cumulative);
    }
    if (report.presented_frames != 0u) {
        coverage.frame_first().record(Ps1OmegaFrameFirstLandmark::first_presentable_framebuffer,
                                      evidence, cumulative);
    }
    if (report.stop_reason == Ps1BootStopReason::commercial_frame_presented) {
        coverage.frame_first().record(Ps1OmegaFrameFirstLandmark::commercial_frame,
                                      evidence, cumulative);
    }
}

std::string segment_text(const WorkItem& item,
                         const Ps1BootReport& report,
                         std::uint64_t epoch,
                         std::uint64_t state_hash) {
    std::ostringstream out;
    out << "format=jojo-omega-infinity-segment-v1\n";
    out << "epoch=" << epoch << '\n';
    out << "evidence=" << (item.evidence == Ps1Max3EvidenceClass::strict ? "strict" : "speculative") << '\n';
    out << "state_hash=0x" << std::hex << state_hash << std::dec << '\n';
    out << "depth=" << item.depth << '\n';
    out << "speculative_depth=" << item.speculative_depth << '\n';
    out << "assumption_count=" << item.assumptions.size() << '\n';
    out << "path_count=" << item.path.size() << '\n';
    out << format_ps1_boot_report(report);
    return out.str();
}

Ps1OmegaAppendResult append_checked(Ps1OmegaEvidenceRecorder& recorder,
                                    Ps1OmegaEvidenceCategory category,
                                    std::uint64_t epoch,
                                    std::span<const std::uint8_t> payload) {
    return recorder.append(category, epoch, payload);
}

bool append_resume(Ps1OmegaEvidenceRecorder& recorder,
                   const std::filesystem::path& root,
                   const Ps1Executable& executable,
                   const std::vector<WorkItem>& queue,
                   std::uint64_t epoch,
                   std::uint64_t epoch_retired,
                   std::uint64_t total_retired,
                   std::uint64_t next_insertion,
                   Ps1OmegaSessionIoStatus& failure_status) {
    (void)root;
    Ps1OmegaResumeState state{};
    state.executable_identity = executable.metadata.fnv1a64_hex;
    state.epoch = epoch;
    state.epoch_retired = epoch_retired;
    state.total_retired = total_retired;
    state.next_insertion_sequence = next_insertion;
    state.pending.reserve(queue.size());
    for (const auto& item : queue) state.pending.push_back(describe(item));
    const auto payload = encode_ps1_omega_resume_state(state);
    const auto saved = append_checked(recorder, Ps1OmegaEvidenceCategory::search, epoch, payload);
    if (!saved) {
        failure_status = saved.status;
        return false;
    }
    return true;
}

} // namespace

bool ps1_omega_infinity_has_resumable_session(const std::filesystem::path& session_root) {
    const auto manifest = load_ps1_omega_session_manifest(session_root);
    if (!manifest) return false;
    const auto resume = load_latest_resume(session_root, manifest.value);
    return resume.has_value() && !resume->pending.empty();
}

Result<Ps1OmegaInfinitySummary> explore_ps1_omega_infinity(
    const Ps1Executable& executable,
    const std::filesystem::path& session_root,
    const Ps1OmegaInfinityOptions& options,
    Ps1OmegaInfinityControl& control,
    Ps1OmegaInfinityProgressCallback progress) {
    if (options.epoch_retired_limit == 0u || options.instruction_quantum == 0u ||
        options.max_session_disk_bytes == 0u) {
        return Result<Ps1OmegaInfinitySummary>::failure(
            ErrorCode::invalid_argument, "invalid zero OMEGA Infinity budget");
    }

    Ps1OmegaEvidenceRecorder recorder(session_root, options.max_session_disk_bytes);
    if (!recorder.ready()) {
        return Result<Ps1OmegaInfinitySummary>::failure(ErrorCode::io_error, recorder.detail());
    }

    const auto omega = ps1_max3_options(Ps1Max3Profile::omega);
    std::vector<WorkItem> queue;
    std::uint64_t epoch = 1u;
    std::uint64_t epoch_retired{};
    std::uint64_t total_retired{};
    std::uint64_t next_insertion{};

    if (const auto resume = load_latest_resume(session_root, recorder.manifest()); resume) {
        if (resume->executable_identity != executable.metadata.fnv1a64_hex) {
            Ps1OmegaInfinitySummary summary{};
            summary.stop_reason = Ps1OmegaInfinityStopReason::invalid_resume_state;
            summary.session_root = session_root;
            return Result<Ps1OmegaInfinitySummary>::success(std::move(summary));
        }
        epoch = resume->epoch;
        epoch_retired = resume->epoch_retired;
        total_retired = resume->total_retired;
        next_insertion = resume->next_insertion_sequence;
        for (const auto& descriptor : resume->pending) {
            auto replayed = replay_descriptor(executable, descriptor, options.instruction_quantum);
            if (!replayed) {
                Ps1OmegaInfinitySummary summary{};
                summary.epoch_count = epoch;
                summary.total_retired = total_retired;
                summary.stop_reason = Ps1OmegaInfinityStopReason::invalid_resume_state;
                summary.session_root = session_root;
                return Result<Ps1OmegaInfinitySummary>::success(std::move(summary));
            }
            queue.push_back(std::move(*replayed));
        }
    } else {
        auto root = Ps1BootRuntime::create(executable);
        if (!root) return Result<Ps1OmegaInfinitySummary>::failure(root.error, root.detail);
        WorkItem item{};
        item.runtime = std::move(root.value);
        item.insertion_sequence = next_insertion++;
        queue.push_back(std::move(item));
    }

    Ps1OmegaCoverage coverage;
    std::set<std::uint64_t> unique_states;
    std::map<std::uint64_t, Ps1Max3SearchScore> best_scores;
    std::uint64_t strict_frontiers{};
    std::uint64_t speculative_frontiers{};
    std::uint64_t max_presented_frames{};
    std::optional<std::uint32_t> latest_strict_pc;
    std::optional<std::uint32_t> latest_strict_address;
    std::uint64_t processed_in_epoch{};

    Ps1OmegaInfinitySummary summary{};
    summary.session_root = session_root;

    const auto update_progress = [&]() {
        if (!progress) return;
        progress(Ps1OmegaInfinityProgress{
            epoch, epoch_retired, total_retired, strict_frontiers,
            speculative_frontiers, recorder.manifest().committed_bytes,
            latest_strict_pc, latest_strict_address,
        });
    };

    const auto persist_queue = [&](Ps1OmegaInfinityStopReason failure_reason) -> bool {
        Ps1OmegaSessionIoStatus status{};
        if (append_resume(recorder, session_root, executable, queue, epoch,
                          epoch_retired, total_retired, next_insertion, status)) {
            return true;
        }
        summary.stop_reason = status == Ps1OmegaSessionIoStatus::disk_budget_exhausted
            ? Ps1OmegaInfinityStopReason::disk_budget_exhausted
            : failure_reason;
        return false;
    };

    while (!queue.empty()) {
        if (control.stop_requested()) {
            summary.stop_reason = Ps1OmegaInfinityStopReason::user_requested;
            persist_queue(Ps1OmegaInfinityStopReason::fatal_no_safe_continuation);
            break;
        }

        if (epoch_retired >= options.epoch_retired_limit || processed_in_epoch >= omega.max_nodes) {
            const auto coverage_payload = encode_ps1_omega_coverage(coverage);
            const auto saved = append_checked(recorder, Ps1OmegaEvidenceCategory::coverage,
                                              epoch, coverage_payload);
            if (!saved) {
                summary.stop_reason = saved.status == Ps1OmegaSessionIoStatus::disk_budget_exhausted
                    ? Ps1OmegaInfinityStopReason::disk_budget_exhausted
                    : Ps1OmegaInfinityStopReason::fatal_no_safe_continuation;
                break;
            }
            if (!persist_queue(Ps1OmegaInfinityStopReason::fatal_no_safe_continuation)) break;
            ++epoch;
            epoch_retired = 0u;
            processed_in_epoch = 0u;
            update_progress();
            continue;
        }

        WorkItem item = pop_best(queue);
        const auto state_before = item.runtime.diagnostic_state_hash();
        unique_states.insert(state_before);
        const auto candidate_score = score(item);
        const auto prior = best_scores.find(state_before);
        if (prior != best_scores.end() && ps1_max3_state_dominates(prior->second, candidate_score)) {
            continue;
        }
        if (prior == best_scores.end() || ps1_max3_search_outranks(candidate_score, prior->second)) {
            best_scores[state_before] = candidate_score;
        }

        const auto remaining_epoch = options.epoch_retired_limit - epoch_retired;
        const auto quantum = std::min(options.instruction_quantum, remaining_epoch);
        auto report = item.runtime.run(segment_options(options, quantum));
        ++processed_in_epoch;
        epoch_retired += report.instructions_retired;
        total_retired += report.instructions_retired;
        item.cumulative_retired += report.instructions_retired;
        item.metrics = accumulate(item.metrics, report);
        max_presented_frames = std::max(max_presented_frames, item.metrics.presented_frames);
        const auto state_after = item.runtime.diagnostic_state_hash();
        unique_states.insert(state_after);
        observe_report(coverage, report, item.evidence, item.cumulative_retired);

        const auto segment_payload = text_bytes(segment_text(item, report, epoch, state_after));
        const auto segment_saved = append_checked(recorder, Ps1OmegaEvidenceCategory::cpu,
                                                  epoch, segment_payload);
        if (!segment_saved) {
            summary.stop_reason = segment_saved.status == Ps1OmegaSessionIoStatus::disk_budget_exhausted
                ? Ps1OmegaInfinityStopReason::disk_budget_exhausted
                : Ps1OmegaInfinityStopReason::fatal_no_safe_continuation;
            break;
        }

        if (report.stop_reason != Ps1BootStopReason::execution_budget_exhausted) {
            const auto snapshot = capture_ps1_omega_snapshot(item.runtime.cpu_state(), item.runtime.bus());
            const auto snapshot_payload = encode_ps1_omega_snapshot(snapshot);
            const auto snapshot_saved = append_checked(recorder, Ps1OmegaEvidenceCategory::snapshot,
                                                       epoch, snapshot_payload);
            if (!snapshot_saved) {
                summary.stop_reason = snapshot_saved.status == Ps1OmegaSessionIoStatus::disk_budget_exhausted
                    ? Ps1OmegaInfinityStopReason::disk_budget_exhausted
                    : Ps1OmegaInfinityStopReason::fatal_no_safe_continuation;
                break;
            }

            if (item.evidence == Ps1Max3EvidenceClass::strict) {
                ++strict_frontiers;
                latest_strict_pc = report.last_pc;
                if (report.unsupported_access) latest_strict_address = report.unsupported_access->physical_address;
            } else {
                ++speculative_frontiers;
            }
        }

        update_progress();

        if (report.stop_reason == Ps1BootStopReason::commercial_frame_presented &&
            item.evidence == Ps1Max3EvidenceClass::strict &&
            options.stop_on_strict_commercial_frame) {
            summary.stop_reason = Ps1OmegaInfinityStopReason::strict_commercial_frame;
            queue.clear();
            break;
        }

        if (report.stop_reason == Ps1BootStopReason::execution_budget_exhausted) {
            item.tail_retired += report.instructions_retired;
            queue.push_back(std::move(item));
            continue;
        }

        const bool can_assume = item.depth < omega.max_branch_depth &&
                                item.speculative_depth < omega.max_speculative_depth;
        if (!can_assume) continue;

        auto enqueue_child = [&](Ps1BootRuntime child_runtime,
                                 Ps1Max3Decision decision) {
            if (queue.size() >= omega.max_queued_states) return;
            WorkItem child{};
            child.runtime = std::move(child_runtime);
            child.evidence = Ps1Max3EvidenceClass::speculative;
            child.depth = item.depth + 1u;
            child.speculative_depth = item.speculative_depth + 1u;
            child.cumulative_retired = item.cumulative_retired;
            child.tail_retired = 0u;
            child.insertion_sequence = next_insertion++;
            child.metrics = item.metrics;
            child.path = item.path;
            child.path.push_back(decision);
            child.assumptions = item.assumptions;
            child.assumptions.push_back(decision);
            queue.push_back(std::move(child));
        };

        if (report.stop_reason == Ps1BootStopReason::bios_call_unimplemented) {
            const auto physical = Ps1MemoryBus::guest_to_physical(item.runtime.cpu_state().pc);
            if (!physical) continue;
            for (const auto fallback : kBiosFallbacks) {
                auto child_runtime = item.runtime;
                if (!child_runtime.apply_diagnostic_bios_fallback(fallback)) continue;
                Ps1Max3Decision decision{};
                decision.kind = Ps1Max3DecisionKind::bios_fallback;
                decision.table = *physical;
                decision.selector = item.runtime.cpu_state().gpr[9];
                decision.fallback = fallback;
                enqueue_child(std::move(child_runtime), decision);
            }
            continue;
        }

        if (report.stop_reason == Ps1BootStopReason::mmio_unimplemented &&
            report.unsupported_access && !report.unsupported_access->write &&
            item.runtime.diagnostic_mmio_read_frontier()) {
            const auto& blocked = *item.runtime.diagnostic_mmio_read_frontier();
            Ps1Max3CandidateContext context{};
            context.profile = Ps1Max3Profile::omega;
            context.width = blocked.access.width;
            context.pc = blocked.pc;
            context.load_opcode = blocked.opcode;
            context.max_candidates = omega.max_candidates_per_read;
            context.following_opcodes = following_opcodes(item.runtime, blocked.pc);
            const auto candidates = generate_ps1_max3_read_candidates(context);
            for (const auto& candidate : candidates) {
                auto child_runtime = item.runtime;
                if (!child_runtime.apply_diagnostic_mmio_read_fallback(candidate.value)) continue;
                Ps1Max3Decision decision{};
                decision.kind = Ps1Max3DecisionKind::mmio_read_fallback;
                decision.address = blocked.access.physical_address;
                decision.width = blocked.access.width;
                decision.value = candidate.value;
                decision.candidate_source = candidate.source;
                enqueue_child(std::move(child_runtime), decision);
            }
            continue;
        }

        if (report.stop_reason == Ps1BootStopReason::mmio_unimplemented &&
            report.unsupported_access && report.unsupported_access->write) {
            auto child_runtime = item.runtime;
            if (child_runtime.apply_diagnostic_mmio_write_no_effect(report)) {
                Ps1Max3Decision decision{};
                decision.kind = Ps1Max3DecisionKind::mmio_write_no_effect;
                decision.address = report.unsupported_access->physical_address;
                decision.width = report.unsupported_access->width;
                decision.value = report.unsupported_access->value;
                decision.candidate_source = Ps1Max3CandidateSource::diagnostic_no_effect;
                enqueue_child(std::move(child_runtime), decision);
            }
        }
    }

    if (summary.stop_reason == Ps1OmegaInfinityStopReason::none) {
        summary.stop_reason = queue.empty()
            ? Ps1OmegaInfinityStopReason::fatal_no_safe_continuation
            : Ps1OmegaInfinityStopReason::user_requested;
    }

    if (queue.empty()) {
        Ps1OmegaSessionIoStatus ignored{};
        (void)append_resume(recorder, session_root, executable, queue, epoch,
                            epoch_retired, total_retired, next_insertion, ignored);
    } else if (summary.stop_reason != Ps1OmegaInfinityStopReason::user_requested) {
        (void)persist_queue(Ps1OmegaInfinityStopReason::fatal_no_safe_continuation);
    }

    const auto coverage_payload = encode_ps1_omega_coverage(coverage);
    (void)append_checked(recorder, Ps1OmegaEvidenceCategory::coverage, epoch, coverage_payload);

    summary.epoch_count = epoch;
    summary.total_retired = total_retired;
    summary.strict_frontier_count = strict_frontiers;
    summary.speculative_frontier_count = speculative_frontiers;
    summary.unique_state_count = unique_states.size();
    summary.presented_frames = max_presented_frames;
    update_progress();
    return Result<Ps1OmegaInfinitySummary>::success(std::move(summary));
}

} // namespace jojo