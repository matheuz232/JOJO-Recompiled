#include "core/ps1_hle_bios.h"

#include "core/ps1_memory_bus.h"

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint32_t kCriticalMask = (1u << 0) | (1u << 10);
constexpr std::uint32_t kC0Table = 0x00000674u;
constexpr std::uint32_t kC0ExceptionEntry = 0x00000C80u;
constexpr std::uint32_t kDefaultEntryInt = 0x00006CF4u;
constexpr std::uint32_t kReturnFromException = 0x00000F40u;
constexpr std::uint32_t kKernelSavedSp = 0x000085D4u;
constexpr std::uint32_t kC0TableWords = 0x1Eu;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_bool(std::uint64_t& hash, bool value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value ? 1u : 0u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
}

void hash_optional_u32(std::uint64_t& hash, const std::optional<std::uint32_t>& value) noexcept {
    hash_bool(hash, value.has_value());
    if (value) hash_u32(hash, *value);
}

void hash_optional_bool(std::uint64_t& hash, const std::optional<bool>& value) noexcept {
    hash_bool(hash, value.has_value());
    if (value) hash_bool(hash, *value);
}

void return_from_bios_vector(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

void return_zero_from_bios_vector(R3000aState& cpu) noexcept {
    cpu.gpr[2] = 0u;
    return_from_bios_vector(cpu);
}

void advance_sys_instruction(R3000aState& cpu) noexcept {
    cpu.pc = cpu.next_pc;
    cpu.next_pc += 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

bool write32_ok(Ps1MemoryBus& bus, std::uint32_t address, std::uint32_t value) noexcept {
    return bus.write32(address, value).status == R3000aBusStatus::ok;
}

bool is_safe_a0_return_zero(std::uint32_t selector) noexcept {
    switch (selector) {
        case 0x57u: case 0x58u: case 0x59u: case 0x5Au:
        case 0x73u: case 0x74u: case 0x75u: case 0x76u: case 0x77u:
        case 0x79u: case 0x7Au: case 0x7Bu: case 0x7Du:
        case 0x7Fu: case 0x80u:
        case 0x82u: case 0x83u: case 0x84u: case 0x85u:
        case 0x86u: case 0x87u: case 0x88u: case 0x89u:
        case 0x8Au: case 0x8Bu: case 0x8Cu: case 0x8Du:
        case 0x8Eu: case 0x8Fu:
        case 0xB0u: case 0xB1u: case 0xB3u:
            return true;
        default:
            return false;
    }
}

bool is_safe_c0_return_zero(std::uint32_t selector) noexcept {
    switch (selector) {
        case 0x0Eu:
        case 0x0Fu:
        case 0x10u:
        case 0x11u:
        case 0x14u:
            return true;
        default:
            return false;
    }
}

} // namespace

Ps1HleBiosResult Ps1HleBios::dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu) noexcept {
    return dispatch_impl(call, cpu, nullptr);
}

Ps1HleBiosResult Ps1HleBios::dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu,
    Ps1MemoryBus& bus) noexcept {
    return dispatch_impl(call, cpu, &bus);
}

Ps1HleBiosResult Ps1HleBios::dispatch_impl(
    const Ps1HleBiosCall& call,
    R3000aState& cpu,
    Ps1MemoryBus* bus) noexcept {
    switch (call.domain) {
        case Ps1HleBiosDomain::a0:
            switch (call.selector) {
                case 0x39u:
                    heap_state_ = Ps1BiosHeapState{call.a0, call.a1};
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                case 0x44u: // FlushCache; interpreter has no instruction cache.
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                case 0x56u:
                case 0x72u:
                    iso9660_removed_ = true;
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                default:
                    break;
            }
            if (is_safe_a0_return_zero(call.selector)) {
                return_zero_from_bios_vector(cpu);
                return {Ps1HleBiosDisposition::handled};
            }
            break;
        case Ps1HleBiosDomain::b0:
            switch (call.selector) {
                case 0x18u: { // ResetEntryInt
                    if (!bus) return {Ps1HleBiosDisposition::unsupported};
                    for (std::uint32_t word = 0u; word < 12u; ++word) {
                        std::uint32_t value = 0u;
                        if (word == 0u) value = kReturnFromException;
                        else if (word == 1u) value = kKernelSavedSp;
                        if (!write32_ok(*bus, kDefaultEntryInt + word * 4u, value)) {
                            return {Ps1HleBiosDisposition::terminal};
                        }
                    }
                    interrupt_hook_address_ = kDefaultEntryInt;
                    cpu.gpr[2] = kDefaultEntryInt;
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                }
                case 0x19u:
                    interrupt_hook_address_ = call.a0;
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                case 0x56u: { // GetC0Table
                    if (!bus) return {Ps1HleBiosDisposition::unsupported};
                    if (!c0_table_materialized_) {
                        for (std::uint32_t word = 0u; word < kC0TableWords; ++word) {
                            const auto value = word == 6u ? kC0ExceptionEntry : 0u;
                            if (!write32_ok(*bus, kC0Table + word * 4u, value)) {
                                return {Ps1HleBiosDisposition::terminal};
                            }
                        }
                        c0_table_materialized_ = true;
                    }
                    cpu.gpr[2] = kC0Table;
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                }
                case 0x5Bu:
                    pad_card_auto_ack_enabled_ = call.a0 != 0u;
                    return_from_bios_vector(cpu);
                    return {Ps1HleBiosDisposition::handled};
                default:
                    break;
            }
            break;
        case Ps1HleBiosDomain::c0:
            if (call.selector == 0x0Au && call.a0 < root_counter_auto_ack_enabled_.size()) {
                const auto index = static_cast<std::size_t>(call.a0);
                const bool previous = root_counter_auto_ack_enabled_[index].value_or(false);
                root_counter_auto_ack_enabled_[index] = call.a1 != 0u;
                cpu.gpr[2] = previous ? 1u : 0u;
                return_from_bios_vector(cpu);
                return {Ps1HleBiosDisposition::handled};
            }
            if (is_safe_c0_return_zero(call.selector)) {
                return_zero_from_bios_vector(cpu);
                return {Ps1HleBiosDisposition::handled};
            }
            break;
        case Ps1HleBiosDomain::sys:
            switch (call.selector) {
                case 0u: // SYS(00h) NoFunction
                    advance_sys_instruction(cpu);
                    return {Ps1HleBiosDisposition::handled};
                case 1u: { // SYS(01h) EnterCriticalSection
                    const bool enabled = (cpu.cop0.status & kCriticalMask) == kCriticalMask;
                    cpu.cop0.status &= ~kCriticalMask;
                    cpu.gpr[2] = enabled ? 1u : 0u;
                    advance_sys_instruction(cpu);
                    return {Ps1HleBiosDisposition::handled};
                }
                case 2u: // SYS(02h) ExitCriticalSection
                    cpu.cop0.status |= kCriticalMask;
                    advance_sys_instruction(cpu);
                    return {Ps1HleBiosDisposition::handled};
                default:
                    break;
            }
            break;
    }
    return {Ps1HleBiosDisposition::unsupported};
}

std::uint64_t Ps1HleBios::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_bool(hash, heap_state_.has_value());
    if (heap_state_) {
        hash_u32(hash, heap_state_->base);
        hash_u32(hash, heap_state_->size);
    }
    hash_optional_u32(hash, interrupt_hook_address_);
    hash_optional_bool(hash, pad_card_auto_ack_enabled_);
    for (const auto& state : root_counter_auto_ack_enabled_) hash_optional_bool(hash, state);
    hash_bool(hash, iso9660_removed_);
    hash_bool(hash, c0_table_materialized_);
    return hash;
}

const std::optional<Ps1BiosHeapState>& Ps1HleBios::heap_state() const noexcept { return heap_state_; }
const std::optional<std::uint32_t>& Ps1HleBios::interrupt_hook_address() const noexcept { return interrupt_hook_address_; }
const std::optional<bool>& Ps1HleBios::pad_card_auto_ack_enabled() const noexcept { return pad_card_auto_ack_enabled_; }

std::optional<bool> Ps1HleBios::root_counter_auto_ack_enabled(std::uint32_t counter) const noexcept {
    if (counter >= root_counter_auto_ack_enabled_.size()) return std::nullopt;
    return root_counter_auto_ack_enabled_[static_cast<std::size_t>(counter)];
}

bool Ps1HleBios::iso9660_removed() const noexcept { return iso9660_removed_; }

} // namespace jojo