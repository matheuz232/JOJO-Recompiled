#pragma once
#include "core/dreamcast_boot.h"
#include "core/result.h"
#include "core/sh4_cfg.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

enum class DreamcastBootEncoding {
    plain_gdrom,
    milcd_requires_normalization,
    unknown,
};

struct UnsupportedOpcodeCount {
    std::uint16_t raw_opcode{};
    std::size_t count{};
};

struct DreamcastBootAnalysis {
    DreamcastBootEncoding encoding{DreamcastBootEncoding::unknown};
    std::uint32_t load_address{0x8C010000u};
    std::size_t word_count{};
    std::size_t supported_word_count{};
    std::size_t unsupported_word_count{};
    std::vector<UnsupportedOpcodeCount> unsupported_histogram;
    Sh4ControlFlowGraph entry_cfg;
};

[[nodiscard]] DreamcastBootEncoding classify_dreamcast_boot_encoding(
    const DreamcastIpMetadata& metadata) noexcept;

[[nodiscard]] Result<DreamcastBootAnalysis> analyze_dreamcast_boot_program(
    const DreamcastBootProgram& program);

}
