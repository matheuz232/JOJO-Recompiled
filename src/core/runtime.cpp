#include "core/runtime.h"

#include "core/game_backend.h"
#include "core/native_backend.h"

#include <cstdint>

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

    const auto& manifest = install.value.manifest;
    if (manifest.backend != "native-ready") {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "converted installation is valid, but the game-specific native recompiler backend is not installed yet");
    }
    if (!supports_game_native_backend(manifest.revision_id)) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "native-ready manifest targets a revision without an enabled game-specific backend");
    }
    if (!has_complete_native_backend_metadata(manifest)) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "native-ready manifest is missing complete native backend metadata");
    }

    const auto plan_path = install_dir / "cache" / "native" / "compiled_plan.bin";
    auto loaded = load_native_backend_cache(plan_path);
    if (!loaded) return Result<void>::failure(loaded.error, loaded.detail);

    std::uint64_t native_code_bytes = 0u;
    for (const auto& block : loaded.value.blocks) {
        native_code_bytes += static_cast<std::uint64_t>(block.native_code.size());
    }

    const auto block_count = static_cast<std::uint64_t>(loaded.value.ir.blocks.size());
    const auto native_block_count = static_cast<std::uint64_t>(loaded.value.native_block_count);
    const auto fallback_block_count = static_cast<std::uint64_t>(loaded.value.fallback_block_count);

    if (loaded.value.abi_version != native_backend_abi_version() ||
        loaded.value.abi_version != *manifest.backend_abi_version ||
        loaded.value.program_hash != manifest.backend_program_hash ||
        block_count != *manifest.backend_block_count ||
        native_block_count != *manifest.backend_native_block_count ||
        fallback_block_count != *manifest.backend_fallback_block_count ||
        native_code_bytes != *manifest.backend_native_code_bytes) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "native backend cache does not match the promoted installation manifest");
    }

    return Result<void>::success();
}

}
