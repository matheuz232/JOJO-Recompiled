#pragma once

#include <cstdint>
#include <optional>

namespace jojo {

enum class Ps1CdromIoStatus : std::uint8_t {
    ok,
    unsupported_register,
    unsupported_command,
};

struct Ps1CdromIoResult {
    Ps1CdromIoStatus status{Ps1CdromIoStatus::unsupported_register};
    std::uint8_t value{};
};

struct Ps1CdromCommandEvent {
    std::uint64_t sequence{};
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
};

class Ps1CdromState {
public:
    void seed_post_bios(std::uint8_t drive_status, std::uint8_t interrupt_enable) noexcept;

    Ps1CdromIoResult read8(std::uint32_t physical) noexcept;
    Ps1CdromIoResult write8(std::uint32_t physical, std::uint8_t value) noexcept;

    bool take_irq_rising_edge() noexcept;
    [[nodiscard]] bool irq_line() const noexcept;
    [[nodiscard]] std::uint64_t command_count() const noexcept;
    [[nodiscard]] const std::optional<Ps1CdromCommandEvent>& last_command_event() const noexcept;
    [[nodiscard]] std::uint8_t index() const noexcept;
    [[nodiscard]] std::uint8_t drive_status() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_enable() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_status() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    void recompute_irq() noexcept;

    std::uint8_t index_{};
    std::uint8_t drive_status_{};
    std::uint8_t interrupt_enable_{};
    std::uint8_t interrupt_status_{};
    std::optional<std::uint8_t> response_{};
    std::uint64_t command_count_{};
    std::optional<Ps1CdromCommandEvent> last_command_event_{};
    bool irq_line_{};
    bool irq_rising_edge_pending_{};
};

} // namespace jojo
