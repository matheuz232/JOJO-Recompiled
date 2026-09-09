#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace test_ps1 {

inline void write_le32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

inline std::vector<std::uint8_t> make_psx_exe() {
    constexpr std::uint32_t entry_pc = 0x80010000u;
    constexpr std::uint32_t initial_gp = 0x80018000u;
    constexpr std::uint32_t text_load_address = 0x80010000u;
    constexpr std::uint32_t text_size = 16u;
    constexpr std::uint32_t stack_base = 0x801FFF00u;
    constexpr std::uint32_t stack_size = 0x00000100u;

    std::vector<std::uint8_t> bytes(0x800u + text_size, 0u);
    constexpr std::string_view magic = "PS-X EXE";
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_le32(bytes, 0x010, entry_pc);
    write_le32(bytes, 0x014, initial_gp);
    write_le32(bytes, 0x018, text_load_address);
    write_le32(bytes, 0x01C, text_size);
    write_le32(bytes, 0x030, stack_base);
    write_le32(bytes, 0x034, stack_size);

    const std::uint8_t payload[16] = {
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x08, 0x24,
        0x02, 0x00, 0x09, 0x24,
        0x21, 0x50, 0x09, 0x01,
    };
    std::copy(std::begin(payload), std::end(payload), bytes.begin() + 0x800);
    return bytes;
}

}
