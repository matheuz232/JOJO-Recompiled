#pragma once

#include "core/ps1_boot_report.h"
#include "core/ps1_exe.h"
#include "core/ps1_hle_bios.h"
#include "core/ps1_interrupt_continuation.h"
#include "core/ps1_memory_bus.h"
#include "core/r3000a_state.h"
#include "core/result.h"

#include <cstdint>
#include <optional>

namespace jojo {

enum class Ps1BiosFallback : std::uint8_t {
    return_zero,
    return_one,
    return_minus_one,
    preserve_v0,
};

struct Ps1DiagnosticMmioReadFrontier {
    std::uint32_t pc{};
    std::uint32_t opcode{};
    Ps1UnsupportedAccess access{};
};

class Ps1BootRuntime {
public:
    Ps1BootRuntime() = default;

    [[nodiscard]] static Result<Ps1BootRuntime> create(const Ps1Executable& executable);
    [[nodiscard]] Ps1BootReport run(const Ps1BootOptions& options) noexcept;
    [[nodiscard]] bool apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept;
    [[nodiscard]] const std::optional<Ps1DiagnosticMmioReadFrontier>&
        diagnostic_mmio_read_frontier() const noexcept;
    [[nodiscard]] bool apply_diagnostic_mmio_read_fallback(std::uint32_t value) noexcept;

    [[nodiscard]] bool apply_diagnostic_mmio_write_no_effect(
        const Ps1BootReport& frontier) noexcept {
        const bool supported_stop =
            frontier.stop_reason == Ps1BootStopReason::mmio_unimplemented ||
            frontier.stop_reason == Ps1BootStopReason::device_command_unimplemented;
        if (!supported_stop ||
            !frontier.unsupported_access ||
            !frontier.unsupported_access->write ||
            !frontier.last_opcode ||
            !frontier.cpu_diagnostic) {
            return false;
        }
        const auto& expected = *frontier.unsupported_access;
        const auto& observed = bus_.last_unsupported_access();
        if (!observed ||
            observed->guest_address != expected.guest_address ||
            observed->physical_address != expected.physical_address ||
            observed->width != expected.width ||
            !observed->write ||
            observed->value != expected.value ||
            cpu_.pc != frontier.last_pc ||
            frontier.cpu_diagnostic->pc != frontier.last_pc) {
            return false;
        }
        const auto fetched = bus_.read32(cpu_.pc);
        if (fetched.status != R3000aBusStatus::ok || fetched.value != *frontier.last_opcode) {
            return false;
        }

        // step_r3000a has already retired any prior delayed load before it reports
        // an unsupported store boundary. Reproduce only the normal store-retirement
        // epilogue: advance control flow and clear the consumed delay slot. No bus
        // or device state is mutated by this diagnostic continuation. This remains
        // speculative even when the bus classified the blocked write as an
        // unsupported device command rather than a generic unsupported MMIO write.
        cpu_.pc = cpu_.next_pc;
        cpu_.next_pc = cpu_.next_pc + 4u;
        if (cpu_.delay_slot.active) cpu_.delay_slot = {};
        cpu_.gpr[0] = 0u;
        bus_.clear_last_unsupported_access();
        return true;
    }

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const R3000aState& cpu_state() const noexcept;
    [[nodiscard]] const std::optional<Ps1BiosHeapState>& bios_heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& bios_interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& bios_pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::optional<bool> bios_root_counter_auto_ack_enabled(std::uint32_t counter) const noexcept;
    [[nodiscard]] bool bios_iso9660_removed() const noexcept;
    [[nodiscard]] Ps1MemoryBus& bus() noexcept;
    [[nodiscard]] const Ps1MemoryBus& bus() const noexcept;

private:
    Ps1MemoryBus bus_{};
    R3000aState cpu_{};
    Ps1HleBios hle_bios_{};
    Ps1InterruptContinuation interrupt_continuation_{};
    bool diagnostic_bios_frontier_pending_{};
    std::optional<Ps1DiagnosticMmioReadFrontier> diagnostic_mmio_read_frontier_{};
};

} // namespace jojo
