#include "core/ps1_boot_runtime.h"

#include "core/ps1_executable_loader.h"
#include "core/r3000a_reference_executor.h"

#include <utility>

namespace jojo {
namespace {

constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosB0 = 0x000000B0u;
constexpr std::uint32_t kBiosC0 = 0x000000C0u;
constexpr std::uint32_t kBiosA0InitHeap = 0x00000039u;
constexpr std::uint32_t kBiosB0HookEntryInt = 0x00000019u;
constexpr std::uint32_t kBiosB0ChangeClearPad = 0x0000005Bu;
constexpr std::uint32_t kBiosC0ChangeClearRCnt = 0x0000000Au;

bool is_bios_table(std::uint32_t physical) noexcept {
    return physical == kBiosA0 ||
           physical == kBiosB0 ||
           physical == kBiosC0;
}

bool is_initial_mmio_window(std::uint32_t physical) noexcept {
    return physical >= 0x1F801000u && physical < 0x1F803000u;
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

bool handle_bios_call(
    R3000aState& cpu,
    std::optional<Ps1BiosHeapState>& heap_state,
    std::optional<std::uint32_t>& interrupt_hook_address,
    std::optional<bool>& pad_card_auto_ack_enabled,
    std::array<std::optional<bool>, 4>& root_counter_auto_ack_enabled,
    std::uint32_t table_physical,
    std::uint32_t selector) noexcept {
    if (table_physical == kBiosA0 && selector == kBiosA0InitHeap) {
        heap_state = Ps1BiosHeapState{cpu.gpr[4], cpu.gpr[5]};
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

    while (report.instructions_retired < options.instruction_budget) {
        report.last_pc = cpu_.pc;

        const auto physical_pc = Ps1MemoryBus::guest_to_physical(cpu_.pc);
        if (physical_pc && is_bios_table(*physical_pc)) {
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
                                 *physical_pc, cpu_.gpr[9])) {
                continue;
            }
            report.stop_reason = Ps1BootStopReason::bios_call_unimplemented;
            return report;
        }

        const auto observed_opcode = bus_.read32(cpu_.pc);
        if (observed_opcode.status == R3000aBusStatus::ok) {
            report.last_opcode = observed_opcode.value;
        } else {
            report.last_opcode.reset();
        }
        record_recent_trace(report, cpu_.pc, report.last_opcode, options.trace_capacity);
        bus_.clear_last_unsupported_access();
        bus_.clear_last_diagnostic_mmio_probe();

        const auto step = step_r3000a(cpu_, bus_);
        if (step.status == R3000aStepStatus::retired) {
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
            }
            ++report.instructions_retired;
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

Ps1MemoryBus& Ps1BootRuntime::bus() noexcept {
    return bus_;
}

const Ps1MemoryBus& Ps1BootRuntime::bus() const noexcept {
    return bus_;
}

} // namespace jojo
