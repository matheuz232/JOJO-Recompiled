#pragma once

#include "core/dreamcast_bus.h"

#include <array>
#include <cstdint>
#include <optional>

namespace jojo {

class DreamcastSystemAsic final : public DreamcastMmioDevice {
public:
    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override;
    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override;

    void raise_normal(std::uint32_t bits) noexcept { status_[0] |= bits; }
    void raise_external(std::uint32_t bits) noexcept { status_[1] |= bits; }
    void raise_error(std::uint32_t bits) noexcept { status_[2] |= bits; }

    [[nodiscard]] std::optional<std::uint8_t> pending_irq_level() const noexcept;

private:
    [[nodiscard]] static std::uint32_t physical_address(std::uint32_t address) noexcept;
    [[nodiscard]] static int status_index(std::uint32_t address) noexcept;
    [[nodiscard]] static int mask_index(std::uint32_t address) noexcept;

    std::array<std::uint32_t, 3> status_{};
    std::array<std::uint32_t, 9> masks_{};
};

}
