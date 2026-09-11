#pragma once

#include "core/ps1_max3_explorer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

struct Ps1Max3ReadCandidate {
    std::uint32_t value{};
    Ps1Max3CandidateSource source{Ps1Max3CandidateSource::baseline_zero};
};

struct Ps1Max3CandidateContext {
    Ps1Max3Profile profile{Ps1Max3Profile::strict};
    std::uint8_t width{};
    std::uint32_t pc{};
    std::uint32_t load_opcode{};
    std::vector<std::uint32_t> following_opcodes;
    std::vector<std::uint32_t> strict_observed_values;
    std::size_t max_candidates{};
};

[[nodiscard]] std::vector<Ps1Max3ReadCandidate>
generate_ps1_max3_read_candidates(const Ps1Max3CandidateContext& context);

} // namespace jojo
