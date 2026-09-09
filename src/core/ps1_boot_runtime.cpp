#include "core/ps1_boot_runtime.h"

#include "core/ps1_executable_loader.h"
#include "core/r3000a_reference_executor.h"

#include <utility>

namespace jojo {
namespace {

constexpr std::size_t kRecentTraceCapacity = 16u;
constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosA0InitHeap = 0x00000039u;

bool is_bios_table(std::uint32_t physical) noexcept {
    return physical == 0x000000A0u ||
           physical == 0x000000B0u ||
           physical == 0x000000C0u;
}

bool is_initial_mmio_window(std::uint32_t physical) noexcept {
    return physical >= 0x1F801000u && physical < 0x1F803000u;
}

void record_recent_trace(Ps1BootReport& report,
                         std::uint32_t pc,
                         const std::optional<std::uint32_t>& opcode) {
    if (report.recent_trace.size() == kRecentTraceCapacity) {
        report.recent_trace.erase(report.recent_trace.begin());
    }
    report.recent_trace.push_back(Ps1TraceSample{pc, opcode});
}

bool handle_bios_call(R3000aState& cpu,
                      std::optional<Ps1BiosHeapState>& heap_state,
                      std::uint32_t table_physical,
                      std::uint32_t selector) noexcept {
    if (table_physical != kBiosA0 || selector != kBiosA0InitHeap) {
        return false;
    }

    heap_state = Ps1BiosHeapState{cpu.gpr[4], cpu.gpr[5]};
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
    return true;
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

    while (report.instructions_retired < options.instruction_budget) {
        report.last_pc = cpu_.pc;

        const auto physical_pc = Ps1MemoryBus::guest_to_physical(cpu_.pc);
        if (physical_pc && is_bios_table(*physical_pc)) {
            ++report.bios_call_count;
            report.recent_bios_calls.push_back(
                Ps1BiosCallSummary{cpu_.pc, *physical_pc, cpu_.gpr[9]});
            if (handle_bios_call(cpu_, bios_heap_state_, *physical_pc, cpu_.gpr[9])) {
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
        record_recent_trace(report, cpu_.pc, report.last_opcode);
        bus_.clear_last_unsupported_access();

        const auto step = step_r3000a(cpu_, bus_);
        if (step.status == R3000aStepStatus::retired) {
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
                report.recent_mmio.push_back(Ps1MmioSummary{
                    step.diagnostic.pc,
                    report.unsupported_access->guest_address,
                    report.unsupported_access->width,
                    report.unsupported_access->write,
                    report.unsupported_access->value,
                });
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

Ps1MemoryBus& Ps1BootRuntime::bus() noexcept {
    return bus_;
}

const Ps1MemoryBus& Ps1BootRuntime::bus() const noexcept {
    return bus_;
}

} // namespace jojo