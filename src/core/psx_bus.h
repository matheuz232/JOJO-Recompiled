#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

enum class PsxBusAccessReason {
    ok,
    misaligned,
    unmapped,
};

struct PsxBusReadU32Result {
    PsxBusAccessReason reason{PsxBusAccessReason::ok};
    std::uint32_t value{};
};

struct PsxBus {
    static constexpr std::size_t main_ram_size = 2u * 1024u * 1024u;
    static constexpr std::uint32_t default_ram_mirror_window = 8u * 1024u * 1024u;
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(main_ram_size, 0u);
};

[[nodiscard]] inline bool psx_bus_virtual_to_physical(std::uint32_t address,
                                                       std::uint32_t& physical) noexcept {
    if (address < 0x20000000u) {
        physical = address;
        return true;
    }
    if (address >= 0x80000000u && address < 0xa0000000u) {
        physical = address - 0x80000000u;
        return true;
    }
    if (address >= 0xa0000000u && address < 0xc0000000u) {
        physical = address - 0xa0000000u;
        return true;
    }
    return false;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_ram_offset(std::uint32_t address,
                                                            std::size_t& offset) noexcept {
    if ((address & 3u) != 0u) return PsxBusAccessReason::misaligned;

    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) {
        return PsxBusAccessReason::unmapped;
    }
    if (physical >= PsxBus::default_ram_mirror_window) {
        return PsxBusAccessReason::unmapped;
    }

    offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusReadU32Result psx_bus_read_u32(const PsxBus& bus,
                                                           std::uint32_t address) noexcept {
    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset(address, offset);
    if (reason != PsxBusAccessReason::ok) return {reason, 0u};

    const auto value = static_cast<std::uint32_t>(bus.ram[offset + 0u]) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 1u]) << 8u) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 2u]) << 16u) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 3u]) << 24u);
    return {PsxBusAccessReason::ok, value};
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_write_u32(PsxBus& bus,
                                                           std::uint32_t address,
                                                           std::uint32_t value) noexcept {
    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset(address, offset);
    if (reason != PsxBusAccessReason::ok) return reason;

    bus.ram[offset + 0u] = static_cast<std::uint8_t>(value);
    bus.ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bus.ram[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bus.ram[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    return PsxBusAccessReason::ok;
}

}
