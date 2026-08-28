#include "core/revision.h"

namespace jojo {
namespace {
std::uint64_t fnv1a64(const std::vector<std::uint8_t>& data) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}
}

Result<GameRevisionMatch> identify_game_revision(
    const Iso9660Image& image,
    const std::vector<GameRevisionProfile>& profiles) {
    for (const auto& profile : profiles) {
        if (profile.revision_id.empty() || profile.files.empty()) continue;
        bool matches = true;
        for (const auto& expected : profile.files) {
            auto file = read_iso9660_file(image, expected.path);
            if (!file) {
                if (file.error == ErrorCode::file_not_found) {
                    matches = false;
                    break;
                }
                return Result<GameRevisionMatch>::failure(file.error, file.detail);
            }
            if (file.value.size() != expected.size_bytes || fnv1a64(file.value) != expected.fnv1a64) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return Result<GameRevisionMatch>::success(GameRevisionMatch{profile.revision_id});
        }
    }
    return Result<GameRevisionMatch>::failure(
        ErrorCode::unknown_revision,
        "disc image does not match any supported game revision profile");
}

}
