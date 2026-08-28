#pragma once
#include "core/conversion.h"
#include "core/result.h"
#include <filesystem>

namespace jojo {

struct InstallationInfo {
    std::filesystem::path install_dir;
    ConversionManifest manifest;
};

[[nodiscard]] Result<InstallationInfo> validate_installation(const std::filesystem::path& install_dir);
[[nodiscard]] Result<void> bootstrap_runtime(const std::filesystem::path& install_dir);

}
