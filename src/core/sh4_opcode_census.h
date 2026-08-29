#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

struct Sh4OpcodeCensusEntry {
    std::uint16_t raw{};
    std::size_t count{};
    std::vector<std::uint32_t> sample_addresses;
};

struct Sh4OpcodeCensus {
    std::size_t total_words{};
    std::size_t supported_words{};
    std::size_t unsupported_words{};
    std::vector<Sh4OpcodeCensusEntry> unsupported;
};

[[nodiscard]] Result<Sh4OpcodeCensus> analyze_sh4_opcode_census(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t base_address,
    std::size_t sample_limit = 4u);

}
