#pragma once

#include "core/dreamcast_bus.h"
#include "core/dreamcast_system_asic.h"

#include <cstdint>

namespace jojo {

class DreamcastPvr2 final : public DreamcastMmioDevice {
public:
    explicit DreamcastPvr2(DreamcastSystemAsic& system_asic) noexcept
        : system_asic_(&system_asic) {}

    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override;
    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override;

    void vblank_begin() noexcept;

    static constexpr std::uint32_t kNormalEventVblankBegin = 1u << 3u;

private:
    DreamcastSystemAsic* system_asic_{};
};

}
