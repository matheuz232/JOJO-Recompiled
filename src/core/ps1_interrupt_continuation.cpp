#include "core/ps1_interrupt_continuation.h"

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_bool(std::uint64_t& hash, bool value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value ? 1u : 0u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

void Ps1InterruptContinuation::begin(const R3000aState& post_exception,
                                     std::uint32_t pre_exception_status,
                                     std::uint32_t resume_pc,
                                     std::uint32_t resume_next_pc) noexcept {
    *this = Ps1InterruptContinuation{};
    for (std::size_t i = 1u; i < saved_.gpr.size(); ++i) {
        saved_.gpr[i] = post_exception.gpr[i];
    }
    saved_.hi = post_exception.hi;
    saved_.lo = post_exception.lo;
    saved_.status = pre_exception_status;
    saved_.resume_pc = resume_pc;
    saved_.resume_next_pc = resume_next_pc;
    phase_ = Ps1InterruptContinuationPhase::dispatch;
}

void Ps1InterruptContinuation::return_from_exception(R3000aState& cpu) noexcept {
    const auto saved = saved_;
    for (std::size_t i = 1u; i < saved.gpr.size(); ++i) {
        cpu.gpr[i] = saved.gpr[i];
    }
    cpu.hi = saved.hi;
    cpu.lo = saved.lo;
    cpu.cop0.status = saved.status;
    cpu.pc = saved.resume_pc;
    cpu.next_pc = saved.resume_next_pc;
    cpu.pending_load = {};
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
    *this = Ps1InterruptContinuation{};
}

bool Ps1InterruptContinuation::active() const noexcept {
    return phase_ != Ps1InterruptContinuationPhase::inactive;
}

Ps1InterruptContinuationPhase Ps1InterruptContinuation::phase() const noexcept {
    return phase_;
}

std::uint64_t Ps1InterruptContinuation::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_byte(hash, static_cast<std::uint8_t>(phase_));
    for (const auto value : saved_.gpr) hash_u32(hash, value);
    hash_u32(hash, saved_.hi);
    hash_u32(hash, saved_.lo);
    hash_u32(hash, saved_.status);
    hash_u32(hash, saved_.resume_pc);
    hash_u32(hash, saved_.resume_next_pc);
    hash_byte(hash, priority_);
    hash_u32(hash, current_node_);
    hash_u32(hash, next_node_);
    hash_u32(hash, first_callback_);
    hash_u32(hash, second_callback_);
    hash_bool(hash, priority_head_loaded_);
    hash_u64(hash, static_cast<std::uint64_t>(visited_count_));
    for (std::size_t i = 0u; i < visited_count_; ++i) {
        hash_u32(hash, visited_nodes_[i]);
    }
    return hash;
}

} // namespace jojo
