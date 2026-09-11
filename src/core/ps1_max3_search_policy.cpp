#include "core/ps1_max3_search_policy.h"

#include <algorithm>

namespace jojo {
namespace {

bool evidence_outranks(Ps1Max3EvidenceClass candidate,
                       Ps1Max3EvidenceClass current) noexcept {
    return candidate == Ps1Max3EvidenceClass::strict &&
           current == Ps1Max3EvidenceClass::speculative;
}

bool evidence_not_worse(Ps1Max3EvidenceClass incumbent,
                        Ps1Max3EvidenceClass candidate) noexcept {
    return incumbent == candidate ||
           incumbent == Ps1Max3EvidenceClass::strict;
}

bool progress_not_worse(const Ps1Max3SearchScore& incumbent,
                        const Ps1Max3SearchScore& candidate) noexcept {
    return incumbent.presented_frames >= candidate.presented_frames &&
           incumbent.vram_write_count >= candidate.vram_write_count &&
           incumbent.gpu_gp0_command_count >= candidate.gpu_gp0_command_count &&
           incumbent.gpu_gp1_command_count >= candidate.gpu_gp1_command_count &&
           incumbent.dma_transfer_count >= candidate.dma_transfer_count &&
           incumbent.cdrom_command_count >= candidate.cdrom_command_count &&
           incumbent.interrupt_callback_progress >= candidate.interrupt_callback_progress &&
           incumbent.new_frontier_count >= candidate.new_frontier_count &&
           incumbent.new_coverage_count >= candidate.new_coverage_count;
}

} // namespace

bool ps1_max3_search_outranks(const Ps1Max3SearchScore& candidate,
                              const Ps1Max3SearchScore& current) noexcept {
    if (candidate.evidence != current.evidence)
        return evidence_outranks(candidate.evidence, current.evidence);
    if (candidate.presented_frames != current.presented_frames)
        return candidate.presented_frames > current.presented_frames;
    if (candidate.vram_write_count != current.vram_write_count)
        return candidate.vram_write_count > current.vram_write_count;
    if (candidate.gpu_gp0_command_count != current.gpu_gp0_command_count)
        return candidate.gpu_gp0_command_count > current.gpu_gp0_command_count;
    if (candidate.gpu_gp1_command_count != current.gpu_gp1_command_count)
        return candidate.gpu_gp1_command_count > current.gpu_gp1_command_count;
    if (candidate.dma_transfer_count != current.dma_transfer_count)
        return candidate.dma_transfer_count > current.dma_transfer_count;
    if (candidate.cdrom_command_count != current.cdrom_command_count)
        return candidate.cdrom_command_count > current.cdrom_command_count;
    if (candidate.interrupt_callback_progress != current.interrupt_callback_progress)
        return candidate.interrupt_callback_progress > current.interrupt_callback_progress;
    if (candidate.new_frontier_count != current.new_frontier_count)
        return candidate.new_frontier_count > current.new_frontier_count;
    if (candidate.new_coverage_count != current.new_coverage_count)
        return candidate.new_coverage_count > current.new_coverage_count;
    if (candidate.assumption_count != current.assumption_count)
        return candidate.assumption_count < current.assumption_count;
    if (candidate.speculative_depth != current.speculative_depth)
        return candidate.speculative_depth < current.speculative_depth;
    if (candidate.retired != current.retired)
        return candidate.retired > current.retired;
    if (candidate.insertion_sequence != current.insertion_sequence)
        return candidate.insertion_sequence < current.insertion_sequence;
    return false;
}

bool ps1_max3_state_dominates(const Ps1Max3SearchScore& incumbent,
                              const Ps1Max3SearchScore& candidate) noexcept {
    if (incumbent.state_hash != candidate.state_hash) return false;
    if (!progress_not_worse(incumbent, candidate)) return false;
    if (!evidence_not_worse(incumbent.evidence, candidate.evidence)) return false;
    if (incumbent.assumption_count > candidate.assumption_count) return false;
    if (incumbent.speculative_depth > candidate.speculative_depth) return false;
    return true;
}

bool ps1_max3_is_exact_cycle(std::span<const std::uint64_t> ancestor_state_hashes,
                             std::uint64_t state_hash,
                             std::size_t max_cycle_repeats) noexcept {
    const auto required_repeats = std::max<std::size_t>(1u, max_cycle_repeats);
    std::size_t repeats = 0u;
    for (const auto ancestor_hash : ancestor_state_hashes) {
        if (ancestor_hash != state_hash) continue;
        ++repeats;
        if (repeats >= required_repeats) return true;
    }
    return false;
}

} // namespace jojo
