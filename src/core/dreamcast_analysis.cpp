#include "core/dreamcast_analysis.h"
#include "core/sh4_decoder.h"
#include <algorithm>
#include <map>
#include <string_view>

namespace jojo {
namespace {
constexpr std::uint32_t kDreamcastProgramLoadAddress = 0x8C010000u;

bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}
}

DreamcastBootEncoding classify_dreamcast_boot_encoding(
    const DreamcastIpMetadata& metadata) noexcept {
    if (starts_with(metadata.device_info, "GD-ROM")) {
        return DreamcastBootEncoding::plain_gdrom;
    }
    if (starts_with(metadata.device_info, "CD-ROM")) {
        return DreamcastBootEncoding::milcd_requires_normalization;
    }
    return DreamcastBootEncoding::unknown;
}

Result<DreamcastBootAnalysis> analyze_dreamcast_boot_program(
    const DreamcastBootProgram& program) {
    const auto encoding = classify_dreamcast_boot_encoding(program.metadata);
    if (encoding == DreamcastBootEncoding::milcd_requires_normalization) {
        return Result<DreamcastBootAnalysis>::failure(
            ErrorCode::unsupported_format,
            "MIL-CD boot programs require scrambling normalization before SH-4 analysis");
    }
    if (encoding == DreamcastBootEncoding::unknown) {
        return Result<DreamcastBootAnalysis>::failure(
            ErrorCode::unsupported_format,
            "Dreamcast boot media type is unknown; refusing to guess program encoding");
    }

    auto decoded = decode_sh4_stream(program.bytes, kDreamcastProgramLoadAddress);
    if (!decoded) {
        return Result<DreamcastBootAnalysis>::failure(decoded.error, decoded.detail);
    }
    if (decoded.value.empty()) {
        return Result<DreamcastBootAnalysis>::failure(
            ErrorCode::invalid_installation, "Dreamcast boot program is empty");
    }

    DreamcastBootAnalysis analysis{};
    analysis.encoding = encoding;
    analysis.load_address = kDreamcastProgramLoadAddress;
    analysis.word_count = decoded.value.size();

    std::map<std::uint16_t, std::size_t> unsupported_counts;
    for (const auto& instruction : decoded.value) {
        if (instruction.op == Sh4Op::unsupported) {
            ++analysis.unsupported_word_count;
            ++unsupported_counts[instruction.raw];
        } else {
            ++analysis.supported_word_count;
        }
    }

    analysis.unsupported_histogram.reserve(unsupported_counts.size());
    for (const auto& [opcode, count] : unsupported_counts) {
        analysis.unsupported_histogram.push_back({opcode, count});
    }
    std::sort(analysis.unsupported_histogram.begin(), analysis.unsupported_histogram.end(),
              [](const UnsupportedOpcodeCount& a, const UnsupportedOpcodeCount& b) {
                  if (a.count != b.count) return a.count > b.count;
                  return a.raw_opcode < b.raw_opcode;
              });

    auto cfg = build_sh4_cfg(program.bytes,
                             kDreamcastProgramLoadAddress,
                             kDreamcastProgramLoadAddress);
    if (!cfg) {
        return Result<DreamcastBootAnalysis>::failure(cfg.error, cfg.detail);
    }
    analysis.entry_cfg = std::move(cfg.value);
    return Result<DreamcastBootAnalysis>::success(std::move(analysis));
}

}
