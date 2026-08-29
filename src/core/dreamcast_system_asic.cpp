#include "core/dreamcast_system_asic.h"

namespace jojo {
namespace {

constexpr std::uint32_t kStatusBase = 0x005F6900u;
constexpr std::uint32_t kMaskBases[] = {
    0x005F6910u,
    0x005F6920u,
    0x005F6930u,
};
constexpr std::uint8_t kIrqLevels[] = {13u, 11u, 9u};

Result<std::uint8_t> unsupported_read() {
    return Result<std::uint8_t>::failure(
        ErrorCode::invalid_argument,
        "Dreamcast System ASIC register is not implemented");
}

Result<void> unsupported_write() {
    return Result<void>::failure(
        ErrorCode::invalid_argument,
        "Dreamcast System ASIC register is not implemented");
}

} // namespace

std::uint32_t DreamcastSystemAsic::physical_address(std::uint32_t address) noexcept {
    if (address >= 0x80000000u && address < 0xE0000000u) {
        return address & 0x1FFFFFFFu;
    }
    return address;
}

int DreamcastSystemAsic::status_index(std::uint32_t address) noexcept {
    const auto physical = physical_address(address);
    if (physical < kStatusBase || physical > kStatusBase + 0x0Bu) return -1;
    const auto offset = physical - kStatusBase;
    const auto index = static_cast<int>(offset / 4u);
    return index >= 0 && index < 3 ? index : -1;
}

int DreamcastSystemAsic::mask_index(std::uint32_t address) noexcept {
    const auto physical = physical_address(address);
    for (int group = 0; group < 3; ++group) {
        const auto base = kMaskBases[group];
        if (physical >= base && physical <= base + 0x0Bu) {
            const auto slot = static_cast<int>((physical - base) / 4u);
            if (slot >= 0 && slot < 3) return group * 3 + slot;
        }
    }
    return -1;
}

Result<std::uint8_t> DreamcastSystemAsic::read8(std::uint32_t address) {
    const auto physical = physical_address(address);
    if (const auto index = status_index(physical); index >= 0) {
        const auto shift = static_cast<unsigned>((physical & 3u) * 8u);
        return Result<std::uint8_t>::success(
            static_cast<std::uint8_t>((status_[static_cast<std::size_t>(index)] >> shift) & 0xFFu));
    }
    if (const auto index = mask_index(physical); index >= 0) {
        const auto shift = static_cast<unsigned>((physical & 3u) * 8u);
        return Result<std::uint8_t>::success(
            static_cast<std::uint8_t>((masks_[static_cast<std::size_t>(index)] >> shift) & 0xFFu));
    }
    return unsupported_read();
}

Result<void> DreamcastSystemAsic::write8(std::uint32_t address, std::uint8_t value) {
    const auto physical = physical_address(address);
    const auto shift = static_cast<unsigned>((physical & 3u) * 8u);
    const auto byte_mask = static_cast<std::uint32_t>(0xFFu) << shift;
    const auto byte_value = static_cast<std::uint32_t>(value) << shift;

    if (const auto index = status_index(physical); index >= 0) {
        status_[static_cast<std::size_t>(index)] &= ~byte_value;
        return Result<void>::success();
    }
    if (const auto index = mask_index(physical); index >= 0) {
        auto& mask = masks_[static_cast<std::size_t>(index)];
        mask = (mask & ~byte_mask) | byte_value;
        return Result<void>::success();
    }
    return unsupported_write();
}

std::optional<std::uint8_t> DreamcastSystemAsic::pending_irq_level() const noexcept {
    for (std::size_t group = 0; group < 3; ++group) {
        const auto mask_base = group * 3u;
        for (std::size_t source = 0; source < 3; ++source) {
            if ((status_[source] & masks_[mask_base + source]) != 0u) {
                return kIrqLevels[group];
            }
        }
    }
    return std::nullopt;
}

}
