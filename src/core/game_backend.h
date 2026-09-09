#pragma once

#include "core/iso9660.h"
#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace jojo {

inline constexpr std::string_view kJojoUsaObservedRevisionId =
    "jojo-usa-observed-b8b5dbf79cdb9fcf";

enum class GameBackendStage {
    boot_analyzed,
    cache_ready,
    cache_verified,
};

using GameBackendProgressCallback = std::function<void(GameBackendStage)>;

struct GameNativeBackendSummary {
    std::string boot_program_hash_hex;
    std::uint32_t abi_version{};
    std::string program_hash;
    std::uint64_t block_count{};
    std::uint64_t native_block_count{};
    std::uint64_t fallback_block_count{};
    std::uint64_t native_code_bytes{};
};

[[nodiscard]] bool supports_game_native_backend(
    std::string_view revision_id) noexcept;

[[nodiscard]] Result<GameNativeBackendSummary> prepare_game_native_backend(
    std::string_view revision_id,
    const Iso9660Image& image,
    const std::filesystem::path& install_dir,
    const GameBackendProgressCallback& on_progress = {});

}
