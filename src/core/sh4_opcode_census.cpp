#include "core/sh4_opcode_census.h"

#include "core/sh4_decoder.h"

#include <algorithm>
#include <unordered_map>

namespace jojo {

Result<Sh4OpcodeCensus> analyze_sh4_opcode_census(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t base_address,
    std::size_t sample_limit) {
    auto decoded = decode_sh4_stream(bytes, base_address);
    if (!decoded) {
        return Result<Sh4OpcodeCensus>::failure(decoded.error, decoded.detail);
    }

    Sh4OpcodeCensus census{};
    census.total_words = decoded.value.size();

    std::unordered_map<std::uint16_t, std::size_t> entry_index;
    for (const auto& instruction : decoded.value) {
        if (instruction.op != Sh4Op::unsupported) {
            ++census.supported_words;
            continue;
        }

        ++census.unsupported_words;
        auto [it, inserted] = entry_index.emplace(instruction.raw, census.unsupported.size());
        if (inserted) {
            census.unsupported.push_back(Sh4OpcodeCensusEntry{instruction.raw, 0u, {}});
        }
        auto& entry = census.unsupported[it->second];
        ++entry.count;
        if (entry.sample_addresses.size() < sample_limit) {
            entry.sample_addresses.push_back(instruction.address);
        }
    }

    std::sort(census.unsupported.begin(), census.unsupported.end(),
              [](const Sh4OpcodeCensusEntry& lhs, const Sh4OpcodeCensusEntry& rhs) {
                  if (lhs.count != rhs.count) return lhs.count > rhs.count;
                  return lhs.raw < rhs.raw;
              });

    return Result<Sh4OpcodeCensus>::success(std::move(census));
}

}
