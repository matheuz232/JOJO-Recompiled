#pragma once

#include <cstdint>

namespace jojo {

class Ps1GpuState {
public:
    [[nodiscard]] bool write_gp0(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t read_gp0() const noexcept;
    [[nodiscard]] bool write_gp1(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t gpu_stat() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] std::uint64_t gp0_command_count() const noexcept;
    [[nodiscard]] std::uint64_t gp1_command_count() const noexcept;
    [[nodiscard]] std::uint64_t command_buffer_reset_count() const noexcept;
    [[nodiscard]] bool display_disabled() const noexcept;
    [[nodiscard]] std::uint8_t dma_direction() const noexcept;
    [[nodiscard]] std::uint16_t display_vram_x() const noexcept;
    [[nodiscard]] std::uint16_t display_vram_y() const noexcept;
    [[nodiscard]] std::uint16_t horizontal_start() const noexcept;
    [[nodiscard]] std::uint16_t horizontal_end() const noexcept;
    [[nodiscard]] std::uint16_t vertical_start() const noexcept;
    [[nodiscard]] std::uint16_t vertical_end() const noexcept;
    [[nodiscard]] std::uint8_t display_mode() const noexcept;

private:
    void reset_control_state() noexcept;

    bool display_disabled_{true};
    std::uint8_t dma_direction_{};
    std::uint16_t display_vram_x_{};
    std::uint16_t display_vram_y_{};
    std::uint16_t horizontal_start_{0x0200u};
    std::uint16_t horizontal_end_{0x0C00u};
    std::uint16_t vertical_start_{0x0010u};
    std::uint16_t vertical_end_{0x0100u};
    std::uint8_t display_mode_{};
    bool irq1_{};
    std::uint32_t gp0_read_latch_{};
    std::uint64_t gp0_command_count_{};
    std::uint64_t gp1_command_count_{};
    std::uint64_t command_buffer_reset_count_{};
};

} // namespace jojo
