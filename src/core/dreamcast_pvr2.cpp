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

}
