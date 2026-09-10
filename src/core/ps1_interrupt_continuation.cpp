#include "core/ps1_interrupt_continuation.h"

#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint32_t kDefaultEntryInt = 0x00006CF4u;

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

bool Ps1InterruptContinuation::remember_node(std::uint32_t node) noexcept {
    if (node == 0u || visited_count_ >= visited_nodes_.size()) return false;
    for (std::size_t i = 0u; i < visited_count_; ++i) {
        if (visited_nodes_[i] == node) return false;
    }
    visited_nodes_[visited_count_++] = node;
    return true;
}

void Ps1InterruptContinuation::launch_callback(
    R3000aState& cpu,
    std::uint32_t callback,
    Ps1InterruptContinuationPhase phase) noexcept {
    cpu.pc = callback;
    cpu.next_pc = callback + 4u;
    cpu.delay_slot = {};
    cpu.pending_load = {};
    cpu.gpr[31] = callback_return_sentinel;
    cpu.gpr[0] = 0u;
    phase_ = phase;
}

Ps1InterruptDriveResult Ps1InterruptContinuation::drive(
    R3000aState& cpu,
    Ps1MemoryBus& bus,
    const Ps1HleBios& bios) noexcept {
    if (!active()) return {Ps1InterruptDriveStatus::terminal};
    if (phase_ == Ps1InterruptContinuationPhase::hook_guest) {
        return {Ps1InterruptDriveStatus::guest_execution};
    }

    if (phase_ == Ps1InterruptContinuationPhase::first_callback) {
        if (cpu.pc != callback_return_sentinel) {
            return {Ps1InterruptDriveStatus::guest_execution};
        }
        if (cpu.gpr[2] != 0u && second_callback_ != 0u) {
            launch_callback(cpu, second_callback_, Ps1InterruptContinuationPhase::second_callback);
            return {Ps1InterruptDriveStatus::guest_execution};
        }
        current_node_ = next_node_;
        phase_ = Ps1InterruptContinuationPhase::dispatch;
    }

    if (phase_ == Ps1InterruptContinuationPhase::second_callback) {
        if (cpu.pc != callback_return_sentinel) {
            return {Ps1InterruptDriveStatus::guest_execution};
        }
        current_node_ = next_node_;
        phase_ = Ps1InterruptContinuationPhase::dispatch;
    }

    while (phase_ == Ps1InterruptContinuationPhase::dispatch) {
        if (priority_ >= 4u) {
            const auto& hook = bios.interrupt_hook_address();
            if (!hook || *hook == kDefaultEntryInt) {
                return_from_exception(cpu);
                return {Ps1InterruptDriveStatus::restored};
            }

            std::array<std::uint32_t, 12> words{};
            for (std::size_t i = 0u; i < words.size(); ++i) {
                const auto word = bus.read32(*hook + static_cast<std::uint32_t>(i * 4u));
                if (word.status != R3000aBusStatus::ok) {
                    return {Ps1InterruptDriveStatus::terminal};
                }
                words[i] = word.value;
            }

            cpu.gpr[31] = words[0];
            cpu.gpr[29] = words[1];
            cpu.gpr[30] = words[2];
            for (std::size_t i = 0u; i < 8u; ++i) cpu.gpr[16u + i] = words[3u + i];
            cpu.gpr[28] = words[11];
            cpu.gpr[2] = 1u;
            cpu.pc = words[0];
            cpu.next_pc = words[0] + 4u;
            cpu.pending_load = {};
            cpu.delay_slot = {};
            cpu.gpr[0] = 0u;
            phase_ = Ps1InterruptContinuationPhase::hook_guest;
            return {Ps1InterruptDriveStatus::guest_execution};
        }

        if (!priority_head_loaded_) {
            current_node_ = bios.interrupt_priority_head(priority_).value_or(0u);
            priority_head_loaded_ = true;
        }

        if (current_node_ == 0u) {
            ++priority_;
            priority_head_loaded_ = false;
            continue;
        }

        if (!remember_node(current_node_)) {
            return {Ps1InterruptDriveStatus::terminal};
        }

        const auto next = bus.read32(current_node_ + 0u);
        if (next.status != R3000aBusStatus::ok) {
            return {Ps1InterruptDriveStatus::terminal};
        }
        const auto second = bus.read32(current_node_ + 4u);
        if (second.status != R3000aBusStatus::ok) {
            return {Ps1InterruptDriveStatus::terminal};
        }
        const auto first = bus.read32(current_node_ + 8u);
        if (first.status != R3000aBusStatus::ok) {
            return {Ps1InterruptDriveStatus::terminal};
        }

        next_node_ = next.value;
        second_callback_ = second.value;
        first_callback_ = first.value;
        if (first_callback_ == 0u) {
            current_node_ = next_node_;
            continue;
        }

        launch_callback(cpu, first_callback_, Ps1InterruptContinuationPhase::first_callback);
        return {Ps1InterruptDriveStatus::guest_execution};
    }

    return {Ps1InterruptDriveStatus::terminal};
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
