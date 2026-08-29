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
    if (profiles.empty()) {
        return Result<GameRevisionMatch>::failure(
            ErrorCode::unknown_revision,
            "no verified revision profiles are registered yet for this game");
    }
    std::string first_mismatch;
    for (const auto& profile : profiles) {
        if (profile.revision_id.empty() || profile.files.empty()) continue;
        bool matches = true;
        for (const auto& expected : profile.files) {
            auto file = read_iso9660_file(image, expected.path);
            if (!file) {
                if (file.error == ErrorCode::file_not_found) {
                    if (first_mismatch.empty()) {
                        first_mismatch = "profile '" + profile.revision_id + "' is missing " + expected.path;
                    }
                    matches = false;
                    break;
                }
                return Result<GameRevisionMatch>::failure(file.error, file.detail);
            }
            if (file.value.size() != expected.size_bytes || fnv1a64(file.value) != expected.fnv1a64) {
                if (first_mismatch.empty()) {
                    first_mismatch = "profile '" + profile.revision_id + "' fingerprint mismatch for " + expected.path;
                }
                matches = false;
                break;
            }
        }
        if (matches) {
            return Result<GameRevisionMatch>::success(GameRevisionMatch{profile.revision_id});
        }
    }
    std::string detail = "disc image does not match any supported game revision profile";
    if (!first_mismatch.empty()) detail += "; " + first_mismatch;
    return Result<GameRevisionMatch>::failure(ErrorCode::unknown_revision, std::move(detail));
}

}
