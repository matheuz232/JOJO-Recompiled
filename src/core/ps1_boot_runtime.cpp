#include "core/ps1_boot_runtime.h"

#include "core/ps1_executable_loader.h"
#include "core/r3000a_reference_executor.h"

#include <set>
#include <utility>

namespace jojo {
namespace {

constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosB0 = 0x000000B0u;
constexpr std::uint32_t kBiosC0 = 0x000000C0u;
constexpr std::uint32_t kBiosA0InitHeap = 0x00000039u;
constexpr std::uint32_t kBiosA0RemoveIso9660 = 0x00000056u;
constexpr std::uint32_t kBiosA0RemoveIso9660Alias = 0x00000072u;
constexpr std::uint32_t kBiosB0HookEntryInt = 0x00000019u;
constexpr std::uint32_t kBiosB0ChangeClearPad = 0x0000005Bu;
constexpr std::uint32_t kBiosC0ChangeClearRCnt = 0x0000000Au;
constexpr std::uint32_t kSyscallEncodingMask = 0xFC00003Fu;
constexpr std::uint32_t kSyscallEncoding = 0x0000000Cu;
constexpr std::uint32_t kInterruptEnableCurrent = 1u << 0;
constexpr std::uint32_t kInterruptMaskBit10 = 1u << 10;
constexpr std::uint32_t kCauseExternalMask = 0x0000FC00u;
constexpr std::uint32_t kCauseInterruptMask = 0x0000FF00u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

bool is_bios_table(std::uint32_t physical) noexcept {
    return physical == kBiosA0 ||
           physical == kBiosB0 ||
           physical == kBiosC0;
}

bool is_initial_mmio_window(std::uint32_t physical) noexcept {
    return physical >= 0x1F801000u && physical < 0x1F803000u;
}

bool is_syscall_opcode(std::uint32_t opcode) noexcept {
    return (opcode & kSyscallEncodingMask) == kSyscallEncoding;
}

std::uint64_t bios_dependency_key(std::uint32_t table, std::uint32_t selector) noexcept {
    return (static_cast<std::uint64_t>(table) << 32u) | selector;
}

std::uint64_t mmio_dependency_key(const Ps1UnsupportedAccess& access) noexcept {
    return (static_cast<std::uint64_t>(access.physical_address) << 16u) |
           (static_cast<std::uint64_t>(access.width) << 8u) |
           static_cast<std::uint64_t>(access.write ? 1u : 0u);
}

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

void hash_optional_u32(std::uint64_t& hash,
                       const std::optional<std::uint32_t>& value) noexcept {
    hash_bool(hash, value.has_value());
    if (value) hash_u32(hash, *value);
}

void hash_optional_bool(std::uint64_t& hash,
                        const std::optional<bool>& value) noexcept {
    hash_bool(hash, value.has_value());
    if (value) hash_bool(hash, *value);
}

void record_recent_trace(Ps1BootReport& report,
                         std::uint32_t pc,
                         const std::optional<std::uint32_t>& opcode,
                         std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_trace.size() == capacity) {
        report.recent_trace.pop_front();
    }
    report.recent_trace.push_back(Ps1TraceSample{pc, opcode});
}

void record_recent_bios(Ps1BootReport& report,
                        const Ps1BiosCallSummary& event,
                        std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_bios_calls.size() == capacity) {
        report.recent_bios_calls.erase(report.recent_bios_calls.begin());
    }
    report.recent_bios_calls.push_back(event);
}

void record_recent_mmio(Ps1BootReport& report,
                        const Ps1MmioSummary& event,
                        std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_mmio.size() == capacity) {
        report.recent_mmio.erase(report.recent_mmio.begin());
    }
    report.recent_mmio.push_back(event);
}

void return_from_bios_call(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

void retire_pending_load_for_hle(R3000aState& cpu) noexcept {
    if (!cpu.pending_load.valid) return;
    if (cpu.pending_load.reg != 0u) {
        cpu.gpr[cpu.pending_load.reg] = cpu.pending_load.value;
    }
    cpu.pending_load = {};
    cpu.gpr[0] = 0u;
}

void advance_hle_instruction(R3000aState& cpu) noexcept {
    cpu.pc = cpu.next_pc;
    cpu.next_pc += 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

bool interrupt_would_preempt_syscall(const R3000aState& cpu) noexcept {
    const auto cause = (cpu.cop0.cause & ~kCauseExternalMask) |
                       (static_cast<std::uint32_t>(cpu.external_interrupt_pending & 0xFCu) << 8);
    return (cpu.cop0.status & kInterruptEnableCurrent) != 0u &&
           (cause & cpu.cop0.status & kCauseInterruptMask) != 0u;
}

bool handle_syscall_hle(R3000aState& cpu, std::uint32_t opcode) noexcept {
    if (!is_syscall_opcode(opcode) || cpu.delay_slot.active ||
        interrupt_would_preempt_syscall(cpu)) {
        return false;
    }

    const std::uint32_t selector = cpu.gpr[4];
    if (selector > 2u) return false;

    retire_pending_load_for_hle(cpu);
    const std::uint32_t critical_mask = kInterruptEnableCurrent | kInterruptMaskBit10;

    switch (selector) {
        case 0u: // SYS(00h) NoFunction
            break;
        case 1u: { // SYS(01h) EnterCriticalSection
            const bool enabled = (cpu.cop0.status & critical_mask) == critical_mask;
            cpu.cop0.status &= ~critical_mask;
            cpu.gpr[2] = enabled ? 1u : 0u;
            break;
        }
        case 2u: // SYS(02h) ExitCriticalSection
            cpu.cop0.status |= critical_mask;
            break;
        default:
            return false;
    }

    advance_hle_instruction(cpu);
    return true;
}

bool handle_bios_call(
    R3000aState& cpu,
    std::optional<Ps1BiosHeapState>& heap_state,
    std::optional<std::uint32_t>& interrupt_hook_address,
    std::optional<bool>& pad_card_auto_ack_enabled,
    std::array<std::optional<bool>, 4>& root_counter_auto_ack_enabled,
    bool& iso9660_removed,
    std::uint32_t table_physical,
    std::uint32_t selector) noexcept {
    if (table_physical == kBiosA0 && selector == kBiosA0InitHeap) {
        heap_state = Ps1BiosHeapState{cpu.gpr[4], cpu.gpr[5]};
        return_from_bios_call(cpu);
        return true;
    }

    if (table_physical == kBiosA0 &&
        (selector == kBiosA0RemoveIso9660 || selector == kBiosA0RemoveIso9660Alias)) {
        iso9660_removed = true;
        return_from_bios_call(cpu);
        return true;
    }

    if (table_physical == kBiosB0 && selector == kBiosB0HookEntryInt) {
        interrupt_hook_address = cpu.gpr[4];
        return_from_bios_call(cpu);
        return true;
    }

    if (table_physical == kBiosB0 && selector == kBiosB0ChangeClearPad) {
        pad_card_auto_ack_enabled = cpu.gpr[4] != 0u;
        return_from_bios_call(cpu);
        return true;
    }

    if (table_physical == kBiosC0 && selector == kBiosC0ChangeClearRCnt &&
        cpu.gpr[4] < root_counter_auto_ack_enabled.size()) {
        const auto index = static_cast<std::size_t>(cpu.gpr[4]);
        const bool previous = root_counter_auto_ack_enabled[index].value_or(false);
        root_counter_auto_ack_enabled[index] = cpu.gpr[5] != 0u;
        cpu.gpr[2] = previous ? 1u : 0u;
        return_from_bios_call(cpu);
        return true;
    }

    return false;
}

} // namespace

Result<Ps1BootRuntime> Ps1BootRuntime::create(const Ps1Executable& executable) {
    Ps1BootRuntime runtime{};
    auto loaded = load_ps1_executable_into_bus(runtime.bus_, executable);
    if (!loaded) {
        return Result<Ps1BootRuntime>::failure(loaded.error, loaded.detail);
    }
    runtime.cpu_ = std::move(loaded.value);
    return Result<Ps1BootRuntime>::success(std::move(runtime));
}

Ps1BootReport Ps1BootRuntime::run(const Ps1BootOptions& options) noexcept {
    Ps1BootReport report{};
    report.last_pc = cpu_.pc;
    report.diagnostic_probe_mode = options.diagnostic_mmio_probe;
    bus_.set_diagnostic_mmio_probe_enabled(options.diagnostic_mmio_probe);

    std::uint64_t instructions_since_progress = 0u;
    std::set<std::uint64_t> observed_bios_dependencies;
    std::set<std::uint64_t> observed_mmio_dependencies;

    while (report.instructions_retired < options.instruction_budget) {
        report.last_pc = cpu_.pc;

        const auto physical_pc = Ps1MemoryBus::guest_to_physical(cpu_.pc);
        if (physical_pc && is_bios_table(*physical_pc)) {
            if (observed_bios_dependencies.insert(
                    bios_dependency_key(*physical_pc, cpu_.gpr[9])).second) {
                instructions_since_progress = 0u;
            }
            ++report.bios_call_count;
            record_recent_bios(report, Ps1BiosCallSummary{
                cpu_.pc,
                *physical_pc,
                cpu_.gpr[9],
                cpu_.gpr[4],
                cpu_.gpr[5],
                cpu_.gpr[6],
                cpu_.gpr[7],
                cpu_.gpr[31],
            }, options.bios_event_capacity);
            if (handle_bios_call(cpu_, bios_heap_state_, bios_interrupt_hook_address_,
                                 bios_pad_card_auto_ack_enabled_,
                                 bios_root_counter_auto_ack_enabled_,
                                 bios_iso9660_removed_, *physical_pc, cpu_.gpr[9])) {
                diagnostic_bios_frontier_pending_ = false;
                continue;
            }
            diagnostic_bios_frontier_pending_ = true;
            report.stop_reason = Ps1BootStopReason::bios_call_unimplemented;
            return report;
        }

        diagnostic_bios_frontier_pending_ = false;
        const auto observed_opcode = bus_.read32(cpu_.pc);
        if (observed_opcode.status == R3000aBusStatus::ok) {
            report.last_opcode = observed_opcode.value;
        } else {
            report.last_opcode.reset();
        }
        record_recent_trace(report, cpu_.pc, report.last_opcode, options.trace_capacity);
        bus_.clear_last_unsupported_access();
        bus_.clear_last_diagnostic_mmio_probe();

        if (report.last_opcode && handle_syscall_hle(cpu_, *report.last_opcode)) {
            ++report.instructions_retired;
            ++instructions_since_progress;
            if (options.stagnation_instruction_limit != 0u &&
                instructions_since_progress >= options.stagnation_instruction_limit) {
                report.stop_reason = Ps1BootStopReason::diagnostic_stall;
                return report;
            }
            continue;
        }

        const auto step = step_r3000a(cpu_, bus_);
        if (step.status == R3000aStepStatus::retired) {
            ++report.instructions_retired;
            ++instructions_since_progress;
            if (const auto& probe = bus_.last_diagnostic_mmio_probe(); probe) {
                ++report.speculative_mmio_count;
                record_recent_mmio(report, Ps1MmioSummary{
                    report.last_pc,
                    probe->guest_address,
                    probe->width,
                    probe->write,
                    probe->value,
                    true,
                }, options.mmio_event_capacity);
                if (observed_mmio_dependencies.insert(mmio_dependency_key(*probe)).second) {
                    instructions_since_progress = 0u;
                }
            }
            if (options.stagnation_instruction_limit != 0u &&
                instructions_since_progress >= options.stagnation_instruction_limit) {
                report.stop_reason = Ps1BootStopReason::diagnostic_stall;
                return report;
            }
            continue;
        }

        report.cpu_diagnostic = step.diagnostic;
        report.unsupported_access = bus_.last_unsupported_access();

        if (step.status == R3000aStepStatus::exception &&
            step.diagnostic.exception_code == R3000aExceptionCode::interrupt) {
            ++report.interrupts_accepted;
        }

        if (report.unsupported_access) {
            const auto physical = Ps1MemoryBus::guest_to_physical(
                report.unsupported_access->guest_address);
            if (physical && is_initial_mmio_window(*physical)) {
                report.stop_reason = Ps1BootStopReason::mmio_unimplemented;
                record_recent_mmio(report, Ps1MmioSummary{
                    step.diagnostic.pc,
                    report.unsupported_access->guest_address,
                    report.unsupported_access->width,
                    report.unsupported_access->write,
                    report.unsupported_access->value,
                    false,
                }, options.mmio_event_capacity);
                return report;
            }
        }

        report.stop_reason = Ps1BootStopReason::cpu_boundary;
        return report;
    }

    report.stop_reason = Ps1BootStopReason::execution_budget_exhausted;
    return report;
}

bool Ps1BootRuntime::apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept {
    if (!diagnostic_bios_frontier_pending_) {
        return false;
    }
    const auto physical_pc = Ps1MemoryBus::guest_to_physical(cpu_.pc);
    if (!physical_pc || !is_bios_table(*physical_pc)) {
        diagnostic_bios_frontier_pending_ = false;
        return false;
    }

    switch (fallback) {
        case Ps1BiosFallback::return_zero:
            cpu_.gpr[2] = 0u;
            break;
        case Ps1BiosFallback::return_one:
            cpu_.gpr[2] = 1u;
            break;
        case Ps1BiosFallback::return_minus_one:
            cpu_.gpr[2] = 0xFFFFFFFFu;
            break;
        case Ps1BiosFallback::preserve_v0:
            break;
    }

    return_from_bios_call(cpu_);
    diagnostic_bios_frontier_pending_ = false;
    return true;
}

std::uint64_t Ps1BootRuntime::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u64(hash, bus_.diagnostic_state_hash());
    for (const auto value : cpu_.gpr) hash_u32(hash, value);
    hash_u32(hash, cpu_.hi);
    hash_u32(hash, cpu_.lo);
    hash_u32(hash, cpu_.pc);
    hash_u32(hash, cpu_.next_pc);
    hash_bool(hash, cpu_.pending_load.valid);
    hash_byte(hash, cpu_.pending_load.reg);
    hash_u32(hash, cpu_.pending_load.value);
    hash_bool(hash, cpu_.delay_slot.active);
    hash_u32(hash, cpu_.delay_slot.branch_pc);
    hash_bool(hash, cpu_.delay_slot.taken);
    hash_u32(hash, cpu_.delay_slot.target);
    hash_u32(hash, cpu_.cop0.target_address);
    hash_u32(hash, cpu_.cop0.bad_vaddr);
    hash_u32(hash, cpu_.cop0.status);
    hash_u32(hash, cpu_.cop0.cause);
    hash_u32(hash, cpu_.cop0.epc);
    hash_byte(hash, cpu_.external_interrupt_pending);

    hash_bool(hash, bios_heap_state_.has_value());
    if (bios_heap_state_) {
        hash_u32(hash, bios_heap_state_->base);
        hash_u32(hash, bios_heap_state_->size);
    }
    hash_optional_u32(hash, bios_interrupt_hook_address_);
    hash_optional_bool(hash, bios_pad_card_auto_ack_enabled_);
    for (const auto& state : bios_root_counter_auto_ack_enabled_) {
        hash_optional_bool(hash, state);
    }
    hash_bool(hash, bios_iso9660_removed_);
    hash_bool(hash, diagnostic_bios_frontier_pending_);
    return hash;
}

const R3000aState& Ps1BootRuntime::cpu_state() const noexcept {
    return cpu_;
}

const std::optional<Ps1BiosHeapState>& Ps1BootRuntime::bios_heap_state() const noexcept {
    return bios_heap_state_;
}

const std::optional<std::uint32_t>&
Ps1BootRuntime::bios_interrupt_hook_address() const noexcept {
    return bios_interrupt_hook_address_;
}

const std::optional<bool>&
Ps1BootRuntime::bios_pad_card_auto_ack_enabled() const noexcept {
    return bios_pad_card_auto_ack_enabled_;
}

std::optional<bool> Ps1BootRuntime::bios_root_counter_auto_ack_enabled(
    std::uint32_t counter) const noexcept {
    if (counter >= bios_root_counter_auto_ack_enabled_.size()) {
        return std::nullopt;
    }
    return bios_root_counter_auto_ack_enabled_[static_cast<std::size_t>(counter)];
}

bool Ps1BootRuntime::bios_iso9660_removed() const noexcept {
    return bios_iso9660_removed_;
}

Ps1MemoryBus& Ps1BootRuntime::bus() noexcept {
    return bus_;
}

const Ps1MemoryBus& Ps1BootRuntime::bus() const noexcept {
    return bus_;
}

} // namespace jojo
