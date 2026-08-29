#include "core/dreamcast_pvr2.h"

namespace jojo {

Result<std::uint8_t> DreamcastPvr2::read8(std::uint32_t) {
    return Result<std::uint8_t>::failure(
        ErrorCode::unsupported_format,
        "PVR2 register reads are not implemented yet");
}

Result<void> DreamcastPvr2::write8(std::uint32_t, std::uint8_t) {
    return Result<void>::failure(
        ErrorCode::unsupported_format,
        "PVR2 register writes are not implemented yet");
}

void DreamcastPvr2::vblank_begin() noexcept {
    system_asic_->raise_normal(kNormalEventVblankBegin);
}

void DreamcastPvr2::configure_scan_timing(DreamcastPvr2ScanTiming timing) noexcept {
    scan_timing_ = timing;
    scanline_ = 0u;
    cycles_into_scanline_ = 0u;
}

void DreamcastPvr2::advance_cycles(std::uint32_t cycles) noexcept {
    if (scan_timing_.cycles_per_scanline == 0u || scan_timing_.scanlines_per_frame == 0u) {
        return;
    }

    cycles_into_scanline_ += cycles;
    while (cycles_into_scanline_ >= scan_timing_.cycles_per_scanline) {
        cycles_into_scanline_ -= scan_timing_.cycles_per_scanline;
        scanline_ = (scanline_ + 1u) % scan_timing_.scanlines_per_frame;

        if (scanline_ == scan_timing_.vblank_begin_scanline) {
            system_asic_->raise_normal(kNormalEventVblankBegin);
        }
        if (scanline_ == scan_timing_.vblank_end_scanline) {
            system_asic_->raise_normal(kNormalEventVblankEnd);
        }
    }
}

}
