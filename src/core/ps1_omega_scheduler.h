#pragma once

#include "core/ps1_max3_search_policy.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace jojo {

struct Ps1OmegaReplayDescriptor {
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    std::uint64_t expected_state_hash{};
    std::uint64_t insertion_sequence{};
    std::uint64_t cumulative_retired{};
    std::uint64_t tail_retired{};
    std::uint64_t presented_frames{};
    std::uint64_t vram_write_count{};
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t dma_transfer_count{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t interrupt_callback_progress{};
    std::uint64_t new_frontier_count{};
    std::uint64_t new_coverage_count{};
    std::uint64_t speculative_depth{};
    std::vector<Ps1Max3Decision> path;
    std::vector<Ps1Max3Decision> assumption_chain;
};

struct Ps1OmegaResumeState {
    std::uint32_t schema_version{1u};
    std::string executable_identity;
    std::uint64_t epoch{1u};
    std::uint64_t epoch_retired{};
    std::uint64_t total_retired{};
    std::uint64_t next_insertion_sequence{};
    std::vector<Ps1OmegaReplayDescriptor> pending;
};

[[nodiscard]] inline Ps1Max3SearchScore ps1_omega_replay_score(
    const Ps1OmegaReplayDescriptor& descriptor) noexcept {
    return Ps1Max3SearchScore{
        descriptor.expected_state_hash,
        descriptor.presented_frames,
        descriptor.vram_write_count,
        descriptor.gpu_gp0_command_count,
        descriptor.gpu_gp1_command_count,
        descriptor.dma_transfer_count,
        descriptor.cdrom_command_count,
        descriptor.interrupt_callback_progress,
        static_cast<std::size_t>(descriptor.new_frontier_count),
        static_cast<std::size_t>(descriptor.new_coverage_count),
        descriptor.evidence,
        descriptor.assumption_chain.size(),
        static_cast<std::size_t>(descriptor.speculative_depth),
        descriptor.cumulative_retired,
        descriptor.insertion_sequence,
    };
}

class Ps1OmegaFrontierScheduler {
public:
    void push(Ps1OmegaReplayDescriptor descriptor) {
        pending_.push_back(std::move(descriptor));
    }

    [[nodiscard]] std::optional<Ps1OmegaReplayDescriptor> pop_best() {
        if (pending_.empty()) return std::nullopt;
        std::size_t best{};
        auto best_score = ps1_omega_replay_score(pending_.front());
        for (std::size_t i = 1u; i < pending_.size(); ++i) {
            const auto score = ps1_omega_replay_score(pending_[i]);
            if (ps1_max3_search_outranks(score, best_score)) {
                best = i;
                best_score = score;
            }
        }
        auto result = std::move(pending_[best]);
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(best));
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return pending_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return pending_.size(); }
    [[nodiscard]] const std::vector<Ps1OmegaReplayDescriptor>& pending() const noexcept {
        return pending_;
    }

private:
    std::vector<Ps1OmegaReplayDescriptor> pending_;
};

namespace omega_scheduler_detail {
class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32u; shift += 8u) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64u; shift += 8u) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void string(std::string_view value) {
        u64(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::uint8_t u8() {
        if (cursor_ >= bytes_.size()) { ok_ = false; return 0u; }
        return bytes_[cursor_++];
    }
    std::uint32_t u32() {
        std::uint32_t value{};
        for (unsigned shift = 0; shift < 32u; shift += 8u) value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value{};
        for (unsigned shift = 0; shift < 64u; shift += 8u) value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    std::string string() {
        const auto size = u64();
        if (!ok_ || size > bytes_.size() - cursor_) { ok_ = false; return {}; }
        std::string value(reinterpret_cast<const char*>(bytes_.data() + cursor_),
                          static_cast<std::size_t>(size));
        cursor_ += static_cast<std::size_t>(size);
        return value;
    }
    bool ok() const noexcept { return ok_; }
    bool finished() const noexcept { return ok_ && cursor_ == bytes_.size(); }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_{};
    bool ok_{true};
};

inline void write_decision(Writer& writer, const Ps1Max3Decision& decision) {
    writer.u32(decision.table);
    writer.u32(decision.selector);
    writer.u8(static_cast<std::uint8_t>(decision.fallback));
    writer.u8(static_cast<std::uint8_t>(decision.kind));
    writer.u32(decision.address);
    writer.u8(decision.width);
    writer.u32(decision.value);
    writer.u8(static_cast<std::uint8_t>(decision.candidate_source));
}

inline Ps1Max3Decision read_decision(Reader& reader) {
    Ps1Max3Decision decision{};
    decision.table = reader.u32();
    decision.selector = reader.u32();
    decision.fallback = static_cast<Ps1BiosFallback>(reader.u8());
    decision.kind = static_cast<Ps1Max3DecisionKind>(reader.u8());
    decision.address = reader.u32();
    decision.width = reader.u8();
    decision.value = reader.u32();
    decision.candidate_source = static_cast<Ps1Max3CandidateSource>(reader.u8());
    return decision;
}

inline void write_descriptor(Writer& writer, const Ps1OmegaReplayDescriptor& descriptor) {
    writer.u8(static_cast<std::uint8_t>(descriptor.evidence));
    writer.u64(descriptor.expected_state_hash);
    writer.u64(descriptor.insertion_sequence);
    writer.u64(descriptor.cumulative_retired);
    writer.u64(descriptor.tail_retired);
    writer.u64(descriptor.presented_frames);
    writer.u64(descriptor.vram_write_count);
    writer.u64(descriptor.gpu_gp0_command_count);
    writer.u64(descriptor.gpu_gp1_command_count);
    writer.u64(descriptor.dma_transfer_count);
    writer.u64(descriptor.cdrom_command_count);
    writer.u64(descriptor.interrupt_callback_progress);
    writer.u64(descriptor.new_frontier_count);
    writer.u64(descriptor.new_coverage_count);
    writer.u64(descriptor.speculative_depth);
    writer.u64(descriptor.path.size());
    for (const auto& decision : descriptor.path) write_decision(writer, decision);
    writer.u64(descriptor.assumption_chain.size());
    for (const auto& decision : descriptor.assumption_chain) write_decision(writer, decision);
}

inline std::optional<Ps1OmegaReplayDescriptor> read_descriptor(Reader& reader) {
    Ps1OmegaReplayDescriptor descriptor{};
    const auto evidence = reader.u8();
    if (evidence > static_cast<std::uint8_t>(Ps1Max3EvidenceClass::speculative)) return std::nullopt;
    descriptor.evidence = static_cast<Ps1Max3EvidenceClass>(evidence);
    descriptor.expected_state_hash = reader.u64();
    descriptor.insertion_sequence = reader.u64();
    descriptor.cumulative_retired = reader.u64();
    descriptor.tail_retired = reader.u64();
    descriptor.presented_frames = reader.u64();
    descriptor.vram_write_count = reader.u64();
    descriptor.gpu_gp0_command_count = reader.u64();
    descriptor.gpu_gp1_command_count = reader.u64();
    descriptor.dma_transfer_count = reader.u64();
    descriptor.cdrom_command_count = reader.u64();
    descriptor.interrupt_callback_progress = reader.u64();
    descriptor.new_frontier_count = reader.u64();
    descriptor.new_coverage_count = reader.u64();
    descriptor.speculative_depth = reader.u64();
    const auto path_count = reader.u64();
    if (!reader.ok() || path_count > 1000000u) return std::nullopt;
    descriptor.path.reserve(static_cast<std::size_t>(path_count));
    for (std::uint64_t i = 0; i < path_count; ++i) descriptor.path.push_back(read_decision(reader));
    const auto assumption_count = reader.u64();
    if (!reader.ok() || assumption_count > 1000000u) return std::nullopt;
    descriptor.assumption_chain.reserve(static_cast<std::size_t>(assumption_count));
    for (std::uint64_t i = 0; i < assumption_count; ++i) descriptor.assumption_chain.push_back(read_decision(reader));
    if (!reader.ok()) return std::nullopt;
    return descriptor;
}
} // namespace omega_scheduler_detail

[[nodiscard]] inline std::vector<std::uint8_t> encode_ps1_omega_replay_descriptor(
    const Ps1OmegaReplayDescriptor& descriptor) {
    omega_scheduler_detail::Writer writer;
    writer.u32(2u);
    omega_scheduler_detail::write_descriptor(writer, descriptor);
    return writer.take();
}

[[nodiscard]] inline std::optional<Ps1OmegaReplayDescriptor> decode_ps1_omega_replay_descriptor(
    std::span<const std::uint8_t> bytes) {
    omega_scheduler_detail::Reader reader(bytes);
    if (reader.u32() != 2u) return std::nullopt;
    auto descriptor = omega_scheduler_detail::read_descriptor(reader);
    if (!descriptor || !reader.finished()) return std::nullopt;
    return descriptor;
}

[[nodiscard]] inline std::vector<std::uint8_t> encode_ps1_omega_resume_state(
    const Ps1OmegaResumeState& state) {
    omega_scheduler_detail::Writer writer;
    writer.u32(1u);
    writer.string(state.executable_identity);
    writer.u64(state.epoch);
    writer.u64(state.epoch_retired);
    writer.u64(state.total_retired);
    writer.u64(state.next_insertion_sequence);
    writer.u64(state.pending.size());
    for (const auto& descriptor : state.pending) omega_scheduler_detail::write_descriptor(writer, descriptor);
    return writer.take();
}

[[nodiscard]] inline std::optional<Ps1OmegaResumeState> decode_ps1_omega_resume_state(
    std::span<const std::uint8_t> bytes) {
    omega_scheduler_detail::Reader reader(bytes);
    if (reader.u32() != 1u) return std::nullopt;
    Ps1OmegaResumeState state{};
    state.executable_identity = reader.string();
    state.epoch = reader.u64();
    state.epoch_retired = reader.u64();
    state.total_retired = reader.u64();
    state.next_insertion_sequence = reader.u64();
    const auto pending_count = reader.u64();
    if (!reader.ok() || pending_count > 1000000u) return std::nullopt;
    state.pending.reserve(static_cast<std::size_t>(pending_count));
    for (std::uint64_t i = 0; i < pending_count; ++i) {
        auto descriptor = omega_scheduler_detail::read_descriptor(reader);
        if (!descriptor) return std::nullopt;
        state.pending.push_back(std::move(*descriptor));
    }
    if (!reader.finished()) return std::nullopt;
    return state;
}

} // namespace jojo
