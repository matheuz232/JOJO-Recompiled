#include "core/dreamcast_bus.h"

namespace jojo {
namespace {

std::uint32_t cache_agnostic_address(std::uint32_t address) noexcept {
    if (address >= 0x80000000u && address < 0xE0000000u) {
        return address & 0x1FFFFFFFu;
    }
    return address;
}

bool in_range(std::uint32_t value, std::uint32_t begin, std::uint32_t end) noexcept {
    return value >= begin && value <= end;
}

}

DreamcastBusRegion classify_dreamcast_bus_region(std::uint32_t address) noexcept {
    if (address >= 0xE0000000u) {
        return DreamcastBusRegion::sh4_internal;
    }

    const auto physical = cache_agnostic_address(address);
    if (in_range(physical, 0x0C000000u, 0x0CFFFFFFu)) {
        return DreamcastBusRegion::main_ram;
    }
    if (in_range(physical, 0x005F6800u, 0x005F69FFu)) {
        return DreamcastBusRegion::system_asic;
    }
    if (in_range(physical, 0x005F6C00u, 0x005F6CFFu)) {
        return DreamcastBusRegion::maple;
    }
    if (in_range(physical, 0x005F7000u, 0x005F7FFFu)) {
        return DreamcastBusRegion::gdrom_g1;
    }
    if (in_range(physical, 0x005F8000u, 0x005F9FFFu)) {
        return DreamcastBusRegion::pvr_registers;
    }
    if (in_range(physical, 0x04000000u, 0x06FFFFFFu)) {
        return DreamcastBusRegion::pvr_vram;
    }
    if (in_range(physical, 0x10000000u, 0x13FFFFFFu)) {
        return DreamcastBusRegion::pvr_ta;
    }
    if (in_range(physical, 0x00700000u, 0x0071FFFFu)) {
        return DreamcastBusRegion::aica_registers;
    }
    if (in_range(physical, 0x00800000u, 0x00FFFFFFu)) {
        return DreamcastBusRegion::aica_wave_ram;
    }
    return DreamcastBusRegion::unknown;
}

void DreamcastReferenceBus::attach_device(DreamcastBusRegion region,
                                          DreamcastMmioDevice& device) noexcept {
    devices_[static_cast<std::size_t>(region)] = &device;
}

void DreamcastReferenceBus::detach_device(DreamcastBusRegion region) noexcept {
    devices_[static_cast<std::size_t>(region)] = nullptr;
}

DreamcastMmioDevice* DreamcastReferenceBus::device_for(DreamcastBusRegion region) const noexcept {
    return devices_[static_cast<std::size_t>(region)];
}

void DreamcastReferenceBus::record_fault(std::uint32_t address,
                                         DreamcastBusAccess access) noexcept {
    if (last_fault_.has_value()) return;
    last_fault_ = DreamcastBusFault{
        address,
        classify_dreamcast_bus_region(address),
        access,
        1u,
    };
}

Result<std::uint8_t> DreamcastReferenceBus::read8(std::uint32_t address) {
    if (memory_ == nullptr) {
        record_fault(address, DreamcastBusAccess::read);
        return Result<std::uint8_t>::failure(
            ErrorCode::invalid_installation,
            "Dreamcast reference bus has no memory backing");
    }

    const auto region = classify_dreamcast_bus_region(address);
    if (region == DreamcastBusRegion::main_ram) {
        auto value = read_dreamcast_u8(*memory_, address);
        if (!value) record_fault(address, DreamcastBusAccess::read);
        return value;
    }

    if (auto* device = device_for(region); device != nullptr) {
        auto value = device->read8(address);
        if (!value) record_fault(address, DreamcastBusAccess::read);
        return value;
    }

    record_fault(address, DreamcastBusAccess::read);
    return Result<std::uint8_t>::failure(
        ErrorCode::invalid_argument,
        "Dreamcast bus region has no attached device");
}

Result<void> DreamcastReferenceBus::write8(std::uint32_t address, std::uint8_t value) {
    if (memory_ == nullptr) {
        record_fault(address, DreamcastBusAccess::write);
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "Dreamcast reference bus has no memory backing");
    }

    const auto region = classify_dreamcast_bus_region(address);
    if (region == DreamcastBusRegion::main_ram) {
        auto stored = write_dreamcast_u8(*memory_, address, value);
        if (!stored) record_fault(address, DreamcastBusAccess::write);
        return stored;
    }

    if (auto* device = device_for(region); device != nullptr) {
        auto stored = device->write8(address, value);
        if (!stored) record_fault(address, DreamcastBusAccess::write);
        return stored;
    }

    record_fault(address, DreamcastBusAccess::write);
    return Result<void>::failure(
        ErrorCode::invalid_argument,
        "Dreamcast bus region has no attached device");
}

}
