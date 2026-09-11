#pragma once

#include "core/ps1_max3_explorer.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace jojo {

struct Ps1OmegaMmioSignature {
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};

    friend bool operator<(const Ps1OmegaMmioSignature& lhs,
                          const Ps1OmegaMmioSignature& rhs) noexcept {
        if (lhs.address != rhs.address) return lhs.address < rhs.address;
        if (lhs.width != rhs.width) return lhs.width < rhs.width;
        return lhs.write < rhs.write;
    }
};

enum class Ps1OmegaFrameFirstLandmark : std::uint8_t {
    first_gp0,
    first_dma_to_gpu,
    first_vram_write,
    first_valid_display_config,
    first_presentable_framebuffer,
    commercial_frame,
    count,
};

class Ps1OmegaFrameFirstEvidence {
public:
    void record(Ps1OmegaFrameFirstLandmark landmark,
                Ps1Max3EvidenceClass evidence,
                std::uint64_t retired) noexcept {
        const auto index = static_cast<std::size_t>(landmark);
        auto& target = evidence == Ps1Max3EvidenceClass::strict
            ? strict_[index]
            : speculative_[index];
        if (!target) target = retired;
    }

    [[nodiscard]] std::optional<std::uint64_t> strict_value(
        Ps1OmegaFrameFirstLandmark landmark) const noexcept {
        return strict_[static_cast<std::size_t>(landmark)];
    }

    [[nodiscard]] std::optional<std::uint64_t> speculative_value(
        Ps1OmegaFrameFirstLandmark landmark) const noexcept {
        return speculative_[static_cast<std::size_t>(landmark)];
    }

    void merge(const Ps1OmegaFrameFirstEvidence& other) noexcept {
        for (std::size_t i = 0; i < strict_.size(); ++i) {
            merge_first(strict_[i], other.strict_[i]);
            merge_first(speculative_[i], other.speculative_[i]);
        }
    }

private:
    static void merge_first(std::optional<std::uint64_t>& target,
                            const std::optional<std::uint64_t>& source) noexcept {
        if (!source) return;
        if (!target || *source < *target) target = source;
    }

    static constexpr std::size_t landmark_count =
        static_cast<std::size_t>(Ps1OmegaFrameFirstLandmark::count);
    std::array<std::optional<std::uint64_t>, landmark_count> strict_{};
    std::array<std::optional<std::uint64_t>, landmark_count> speculative_{};
};

class Ps1OmegaCoverage {
public:
    void observe_pc(std::uint32_t pc) { pcs_.insert(pc); }

    void observe_edge(std::uint32_t from, std::uint32_t to) {
        edges_.insert({from, to});
        pcs_.insert(from);
        pcs_.insert(to);
    }

    void observe_mmio(std::uint32_t address, std::uint8_t width, bool write) {
        mmio_.insert(Ps1OmegaMmioSignature{address, width, write});
    }

    void observe_execution(std::uint32_t pc,
                           std::uint32_t next_pc,
                           std::uint64_t state_hash) {
        observe_edge(pc, next_pc);
        ++execution_observations_;
        ++hot_pc_counts_[pc];
        const auto found = last_state_observation_.find(state_hash);
        if (found != last_state_observation_.end()) {
            ++repeated_state_count_;
            last_loop_period_ = execution_observations_ - found->second;
        }
        last_state_observation_[state_hash] = execution_observations_;
    }

    void merge(const Ps1OmegaCoverage& other) {
        pcs_.insert(other.pcs_.begin(), other.pcs_.end());
        edges_.insert(other.edges_.begin(), other.edges_.end());
        mmio_.insert(other.mmio_.begin(), other.mmio_.end());
        execution_observations_ += other.execution_observations_;
        repeated_state_count_ += other.repeated_state_count_;
        if (other.last_loop_period_ != 0u) last_loop_period_ = other.last_loop_period_;
        for (const auto& [pc, count] : other.hot_pc_counts_) hot_pc_counts_[pc] += count;
        frame_first_.merge(other.frame_first_);
    }

    [[nodiscard]] std::size_t unique_pc_count() const noexcept { return pcs_.size(); }
    [[nodiscard]] std::size_t unique_edge_count() const noexcept { return edges_.size(); }
    [[nodiscard]] std::size_t unique_mmio_count() const noexcept { return mmio_.size(); }
    [[nodiscard]] std::uint64_t execution_observation_count() const noexcept {
        return execution_observations_;
    }
    [[nodiscard]] std::uint64_t repeated_state_count() const noexcept {
        return repeated_state_count_;
    }
    [[nodiscard]] std::uint64_t last_loop_period() const noexcept { return last_loop_period_; }
    [[nodiscard]] std::uint64_t hot_pc_count(std::uint32_t pc) const noexcept {
        const auto found = hot_pc_counts_.find(pc);
        return found == hot_pc_counts_.end() ? 0u : found->second;
    }

    [[nodiscard]] Ps1OmegaFrameFirstEvidence& frame_first() noexcept { return frame_first_; }
    [[nodiscard]] const Ps1OmegaFrameFirstEvidence& frame_first() const noexcept {
        return frame_first_;
    }

    [[nodiscard]] const std::set<std::uint32_t>& pcs() const noexcept { return pcs_; }
    [[nodiscard]] const std::set<std::pair<std::uint32_t, std::uint32_t>>& edges() const noexcept {
        return edges_;
    }
    [[nodiscard]] const std::set<Ps1OmegaMmioSignature>& mmio() const noexcept { return mmio_; }

private:
    std::set<std::uint32_t> pcs_;
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges_;
    std::set<Ps1OmegaMmioSignature> mmio_;
    std::map<std::uint32_t, std::uint64_t> hot_pc_counts_;
    std::map<std::uint64_t, std::uint64_t> last_state_observation_;
    std::uint64_t execution_observations_{};
    std::uint64_t repeated_state_count_{};
    std::uint64_t last_loop_period_{};
    Ps1OmegaFrameFirstEvidence frame_first_{};
};

namespace omega_coverage_detail {
inline void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}
inline void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}
} // namespace omega_coverage_detail

[[nodiscard]] inline std::vector<std::uint8_t> encode_ps1_omega_coverage(
    const Ps1OmegaCoverage& coverage) {
    std::vector<std::uint8_t> out;
    omega_coverage_detail::append_u32(out, 1u);
    omega_coverage_detail::append_u64(out, coverage.pcs().size());
    for (const auto pc : coverage.pcs()) omega_coverage_detail::append_u32(out, pc);
    omega_coverage_detail::append_u64(out, coverage.edges().size());
    for (const auto& edge : coverage.edges()) {
        omega_coverage_detail::append_u32(out, edge.first);
        omega_coverage_detail::append_u32(out, edge.second);
    }
    omega_coverage_detail::append_u64(out, coverage.mmio().size());
    for (const auto& mmio : coverage.mmio()) {
        omega_coverage_detail::append_u32(out, mmio.address);
        out.push_back(mmio.width);
        out.push_back(static_cast<std::uint8_t>(mmio.write ? 1u : 0u));
    }
    return out;
}

} // namespace jojo
