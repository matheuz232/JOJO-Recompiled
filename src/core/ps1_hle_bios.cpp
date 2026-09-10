#include "core/ps1_hle_bios.h"

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void return_from_bios_vector(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

} // namespace

Ps1HleBiosResult Ps1HleBios::dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu) noexcept {
    if (call.domain == Ps1HleBiosDomain::a0 && call.selector == 0x39u) {
        heap_state_ = Ps1BiosHeapState{call.a0, call.a1};
        return_from_bios_vector(cpu);
        return {Ps1HleBiosDisposition::handled};
    }
    return {Ps1HleBiosDisposition::unsupported};
}

std::uint64_t Ps1HleBios::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_byte(hash, heap_state_.has_value() ? 1u : 0u);
    if (heap_state_) {
        hash_u32(hash, heap_state_->base);
        hash_u32(hash, heap_state_->size);
    }
    return hash;
}

const std::optional<Ps1BiosHeapState>& Ps1HleBios::heap_state() const noexcept {
    return heap_state_;
}

const std::optional<std::uint32_t>& Ps1HleBios::interrupt_hook_address() const noexcept {
    return interrupt_hook_address_;
}

const std::optional<bool>& Ps1HleBios::pad_card_auto_ack_enabled() const noexcept {
    return pad_card_auto_ack_enabled_;
}

std::optional<bool> Ps1HleBios::root_counter_auto_ack_enabled(
    std::uint32_t counter) const noexcept {
    if (counter >= root_counter_auto_ack_enabled_.size()) return std::nullopt;
    return root_counter_auto_ack_enabled_[static_cast<std::size_t>(counter)];
}

bool Ps1HleBios::iso9660_removed() const noexcept {
    return iso9660_removed_;
}

} // namespace jojo
