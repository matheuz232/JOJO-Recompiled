#pragma once

#include "core/dreamcast_boot.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

inline constexpr std::size_t kDreamcastMainRamSize = 16u * 1024u * 1024u;
inline constexpr std::uint32_t kDreamcastMainRamPhysicalBase = 0x0C000000u;
inline constexpr std::uint32_t kDreamcastMainRamCachedBase = 0x8C000000u;
inline constexpr std::uint32_t kDreamcastMainRamUncachedBase = 0xAC000000u;
inline constexpr std::uint32_t kDreamcastBootLoadAddress = 0x8C010000u;

struct DreamcastExecutableMemory {
    std::vector<std::uint8_t> main_ram;
    std::uint32_t load_address{};
    std::uint32_t entry_pc{};
    std::size_t program_size{};
};

[[nodiscard]] Result<DreamcastExecutableMemory> load_dreamcast_boot_memory(
    const DreamcastBootProgram& program,
    std::uint32_t load_address = kDreamcastBootLoadAddress);

[[nodiscard]] Result<std::uint8_t> read_dreamcast_u8(
    const DreamcastExecutableMemory& memory,
    std::uint32_t address);
[[nodiscard]] Result<std::uint16_t> read_dreamcast_u16(
    const DreamcastExecutableMemory& memory,
    std::uint32_t address);
[[nodiscard]] Result<std::uint32_t> read_dreamcast_u32(
    const DreamcastExecutableMemory& memory,
    std::uint32_t address);

[[nodiscard]] Result<void> write_dreamcast_u8(
    DreamcastExecutableMemory& memory,
    std::uint32_t address,
    std::uint8_t value);
[[nodiscard]] Result<void> write_dreamcast_u16(
    DreamcastExecutableMemory& memory,
    std::uint32_t address,
    std::uint16_t value);
[[nodiscard]] Result<void> write_dreamcast_u32(
    DreamcastExecutableMemory& memory,
    std::uint32_t address,
    std::uint32_t value);

}
