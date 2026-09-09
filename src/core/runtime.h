#pragma once
#include "core/conversion.h"
#include "core/result.h"
#include <filesystem>

namespace jojo {

enum class InstallationKind {
    absent,
    legacy_v1,
    ps1_m1
};

struct InstallationInfo {
    std::filesystem::path install_root;
    std::filesystem::path generation_dir;
    ConversionManifest manifest;
};

[[nodiscard]] Result<InstallationKind> classify_installation(
    const std::filesystem::path& install_root);
[[nodiscard]] Result<InstallationInfo> validate_installation(
    const std::filesystem::path& install_root);
[[nodiscard]] Result<void> bootstrap_runtime(
    const std::filesystem::path& install_root);

}
