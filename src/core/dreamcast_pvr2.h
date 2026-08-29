#pragma once

#include "core/dreamcast_bus.h"
#include "core/dreamcast_system_asic.h"

#include <cstdint>

namespace jojo {

struct DreamcastPvr2ScanTiming {
    std::uint32_t cycles_per_scanline{};
    std::uint32_t scanlines_per_frame{};
    std::uint32_t vblank_begin_scanline{};
    std::uint32_t vblank_end_scanline{};
};

class DreamcastPvr2 final : public DreamcastMmioDevice {
public:
    explicit DreamcastPvr2(DreamcastSystemAsic& system_asic) noexcept
        : system_asic_(&system_asic) {}

    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override;
    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override;

    void vblank_begin() noexcept;
    void configure_scan_timing(DreamcastPvr2ScanTiming timing) noexcept;
    void advance_cycles(std::uint32_t cycles) noexcept;

    static constexpr std::uint32_t kNormalEventVblankBegin = 1u << 3u;
    static constexpr std::uint32_t kNormalEventVblankEnd = 1u << 4u;

private:
    DreamcastSystemAsic* system_asic_{};
    DreamcastPvr2ScanTiming scan_timing_{};
    std::uint32_t scanline_{};
    std::uint32_t cycles_into_scanline_{};
    std::uint32_t spg_vblank_int_{};
    std::uint32_t spg_load_{};
};

}
