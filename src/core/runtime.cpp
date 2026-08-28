#include "core/runtime.h"

namespace jojo {

Result<InstallationInfo> validate_installation(const std::filesystem::path& install_dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(install_dir, ec) || ec) {
        return Result<InstallationInfo>::failure(ErrorCode::invalid_installation,
                                                 "installation directory does not exist");
    }
    if (!std::filesystem::is_directory(install_dir / "data", ec) || ec) {
        return Result<InstallationInfo>::failure(ErrorCode::invalid_installation,
                                                 "installation is missing data directory");
    }
    if (!std::filesystem::is_directory(install_dir / "cache", ec) || ec) {
        return Result<InstallationInfo>::failure(ErrorCode::invalid_installation,
                                                 "installation is missing cache directory");
    }
    auto manifest = load_conversion_manifest(install_dir / "game_manifest.ini");
    if (!manifest) {
        return Result<InstallationInfo>::failure(manifest.error, manifest.detail);
    }
    return Result<InstallationInfo>::success(InstallationInfo{install_dir, std::move(manifest.value)});
}

Result<void> bootstrap_runtime(const std::filesystem::path& install_dir) {
    auto install = validate_installation(install_dir);
    if (!install) return Result<void>::failure(install.error, install.detail);
    if (install.value.manifest.backend != "native-ready") {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "converted installation is valid, but the game-specific native recompiler backend is not installed yet");
    }
    return Result<void>::success();
}

}
