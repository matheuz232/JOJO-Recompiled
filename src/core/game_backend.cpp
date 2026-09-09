#include "core/game_backend.h"

#include "core/dreamcast_analysis.h"
#include "core/dreamcast_boot.h"
#include "core/native_backend.h"

#include <cstdint>
#include <utility>

namespace jojo {

bool supports_game_native_backend(std::string_view revision_id) noexcept {
    return revision_id == kJojoUsaObservedRevisionId;
}

Result<GameNativeBackendSummary> prepare_game_native_backend(
    std::string_view revision_id,
    const Iso9660Image& image,
    const std::filesystem::path& install_dir,
    const GameBackendProgressCallback& on_progress) {
    if (!supports_game_native_backend(revision_id)) {
        return Result<GameNativeBackendSummary>::failure(
            ErrorCode::backend_unavailable,
            "no game-specific native backend is enabled for this revision");
    }

    auto boot = read_dreamcast_boot_program(image);
    if (!boot) {
        return Result<GameNativeBackendSummary>::failure(boot.error, boot.detail);
    }

    auto analysis = analyze_dreamcast_boot_program(boot.value);
    if (!analysis) {
        return Result<GameNativeBackendSummary>::failure(analysis.error, analysis.detail);
    }
    if (on_progress) on_progress(GameBackendStage::boot_analyzed);

    auto cache = ensure_native_backend_cache(boot.value, install_dir);
    if (!cache) {
        return Result<GameNativeBackendSummary>::failure(cache.error, cache.detail);
    }
    if (on_progress) on_progress(GameBackendStage::cache_ready);

    auto loaded = load_native_backend_cache(cache.value.plan_path);
    if (!loaded) {
        return Result<GameNativeBackendSummary>::failure(loaded.error, loaded.detail);
    }

    std::uint64_t native_code_bytes = 0u;
    for (const auto& compiled : loaded.value.blocks) {
        native_code_bytes += static_cast<std::uint64_t>(compiled.native_code.size());
    }

    if (loaded.value.abi_version != native_backend_abi_version() ||
        loaded.value.abi_version != cache.value.abi_version ||
        loaded.value.program_hash != cache.value.program_hash ||
        loaded.value.ir.blocks.size() != cache.value.block_count ||
        native_code_bytes != cache.value.native_code_bytes) {
        return Result<GameNativeBackendSummary>::failure(
            ErrorCode::invalid_installation,
            "native backend cache reload verification failed");
    }

    const auto block_count = static_cast<std::uint64_t>(loaded.value.ir.blocks.size());
    const auto native_block_count = static_cast<std::uint64_t>(loaded.value.native_block_count);
    const auto fallback_block_count = static_cast<std::uint64_t>(loaded.value.fallback_block_count);
    if (native_block_count > block_count || fallback_block_count != block_count - native_block_count) {
        return Result<GameNativeBackendSummary>::failure(
            ErrorCode::invalid_installation,
            "native backend block accounting is inconsistent");
    }

    GameNativeBackendSummary summary{};
    summary.boot_program_hash_hex = boot.value.hash_hex;
    summary.abi_version = native_backend_abi_version();
    summary.program_hash = cache.value.program_hash;
    summary.block_count = block_count;
    summary.native_block_count = native_block_count;
    summary.fallback_block_count = fallback_block_count;
    summary.native_code_bytes = native_code_bytes;

    if (on_progress) on_progress(GameBackendStage::cache_verified);
    return Result<GameNativeBackendSummary>::success(std::move(summary));
}

}
