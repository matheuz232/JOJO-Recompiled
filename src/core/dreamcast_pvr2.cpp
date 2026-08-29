#include "core/dreamcast_pvr2.h"

namespace jojo {
namespace {

constexpr std::uint32_t kPvrBase = 0x005F8000u;
constexpr std::uint32_t kSpgVblankInt = kPvrBase + 0x00CCu;
constexpr std::uint32_t kSpgLoad = kPvrBase + 0x00D8u;

std::uint32_t physical_address(std::uint32_t address) noexcept {
    if (address >= 0x80000000u && address < 0xE0000000u) {
        return address & 0x1FFFFFFFu;
    }
    return address;
}

bool register_byte(std::uint32_t address, std::uint32_t base) noexcept {
    return address >= base && address <= base + 3u;
}

std::uint8_t read_register_byte(std::uint32_t value, std::uint32_t address) noexcept {
    const auto shift = static_cast<unsigned>((address & 3u) * 8u);
    return static_cast<std::uint8_t>((value >> shift) & 0xFFu);
}

void write_register_byte(std::uint32_t& target,
                         std::uint32_t address,
                         std::uint8_t value) noexcept {
    const auto shift = static_cast<unsigned>((address & 3u) * 8u);
    const auto byte_mask = static_cast<std::uint32_t>(0xFFu) << shift;
    target = (target & ~byte_mask) | (static_cast<std::uint32_t>(value) << shift);
}

}

Result<std::uint8_t> DreamcastPvr2::read8(std::uint32_t address) {
    const auto physical = physical_address(address);
    if (register_byte(physical, kSpgVblankInt)) {
        return Result<std::uint8_t>::success(read_register_byte(spg_vblank_int_, physical));
    }
    if (register_byte(physical, kSpgLoad)) {
        return Result<std::uint8_t>::success(read_register_byte(spg_load_, physical));
    }
    return Result<std::uint8_t>::failure(
        ErrorCode::unsupported_format,
        "PVR2 register read is not implemented");
}

Result<void> DreamcastPvr2::write8(std::uint32_t address, std::uint8_t value) {
    const auto physical = physical_address(address);
    if (register_byte(physical, kSpgVblankInt)) {
        write_register_byte(spg_vblank_int_, physical, value);
        scan_timing_.vblank_begin_scanline = spg_vblank_int_ & 0x3FFu;
        scan_timing_.vblank_end_scanline = (spg_vblank_int_ >> 16u) & 0x3FFu;
        return Result<void>::success();
    }
    if (register_byte(physical, kSpgLoad)) {
        write_register_byte(spg_load_, physical, value);
        scan_timing_.cycles_per_scanline = (spg_load_ & 0x3FFu) + 1u;
        scan_timing_.scanlines_per_frame = ((spg_load_ >> 16u) & 0x3FFu) + 1u;
        scanline_ = 0u;
        cycles_into_scanline_ = 0u;
        return Result<void>::success();
    }
    return Result<void>::failure(
        ErrorCode::unsupported_format,
        "PVR2 register write is not implemented");
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
