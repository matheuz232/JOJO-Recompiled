#pragma once
#include "core/result.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace jojo {

enum class ConversionStage {
    validating_source,
    fingerprinting_source,
    preparing_installation,
    writing_manifest,
    completed
};

struct ConversionProgress {
    ConversionStage stage{ConversionStage::validating_source};
    int percent{};
    std::string message_key;
    std::string detail;
};

using ConversionProgressCallback = std::function<void(const ConversionProgress&)>;

struct ConversionManifest {
    std::string manifest_version{"1"};
    std::string converter_version;
    std::string source_name;
    std::string source_format;
    std::uint64_t source_size{};
    std::string hash_hex;
    std::string backend{"pending-game-specific-recompiler"};
};

[[nodiscard]] Result<ConversionManifest> convert_image(
    const std::filesystem::path& source,
    const std::filesystem::path& install_dir,
    const ConversionProgressCallback& on_progress = {});
[[nodiscard]] Result<ConversionManifest> load_conversion_manifest(
    const std::filesystem::path& manifest_path);
[[nodiscard]] Result<void> save_conversion_manifest_atomic(
    const std::filesystem::path& manifest_path,
    const ConversionManifest& manifest);

}
