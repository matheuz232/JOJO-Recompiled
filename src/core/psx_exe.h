#pragma once
#include "core/result.h"
#include <cstdint>
#include <span>

namespace jojo {

struct PsxExeHeader {
    std::uint32_t initial_pc{};
    std::uint32_t initial_gp{};
    std::uint32_t load_address{};
    std::uint32_t payload_size{};
    std::uint32_t data_start{};
    std::uint32_t data_size{};
    std::uint32_t bss_start{};
    std::uint32_t bss_size{};
    std::uint32_t stack_base{};
    std::uint32_t stack_offset{};
};

[[nodiscard]] Result<PsxExeHeader> parse_psx_exe(std::span<const std::uint8_t> file);

}
