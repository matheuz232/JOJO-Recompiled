#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace jojo {

using Sha256Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] Result<Sha256Digest> sha256_file(const std::filesystem::path& path);
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

}
