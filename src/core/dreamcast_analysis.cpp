#include "core/dreamcast_analysis.h"
#include "core/sh4_opcode_census.h"
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

    auto census = analyze_sh4_opcode_census(program.bytes, kDreamcastProgramLoadAddress);
    if (!census) {
        return Result<DreamcastBootAnalysis>::failure(census.error, census.detail);
    }
    if (census.value.total_words == 0u) {
        return Result<DreamcastBootAnalysis>::failure(
            ErrorCode::invalid_installation, "Dreamcast boot program is empty");
    }

    DreamcastBootAnalysis analysis{};
    analysis.encoding = encoding;
    analysis.load_address = kDreamcastProgramLoadAddress;
    analysis.word_count = census.value.total_words;
    analysis.supported_word_count = census.value.supported_words;
    analysis.unsupported_word_count = census.value.unsupported_words;
    analysis.unsupported_histogram.reserve(census.value.unsupported.size());
    for (auto& entry : census.value.unsupported) {
        analysis.unsupported_histogram.push_back(
            UnsupportedOpcodeCount{entry.raw, entry.count, std::move(entry.sample_addresses)});
    }

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
