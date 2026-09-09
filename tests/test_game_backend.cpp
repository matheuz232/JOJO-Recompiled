#include "core/game_backend.h"
#include "core/iso9660.h"
#include "core/native_backend.h"
#include "iso_fixture.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static constexpr std::array<std::uint8_t, 12> kValidBoot{{
    0x01, 0xE0,
    0x02, 0x70,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
}};

static constexpr std::array<std::uint8_t, 12> kUnsupportedBoot{{
    0xFF, 0xFF,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
}};

static fs::path temp_root(const char* suffix) {
    auto root = fs::temp_directory_path() / (std::string("jojo-game-backend-") + suffix);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

static void test_supported_usa_revision_builds_and_reloads_native_cache() {
    const auto root = temp_root("supported");
    const auto image_path = root / "synthetic.iso";
    test_iso::write_image(image_path);
    test_iso::install_dreamcast_ip_metadata(image_path);
    test_iso::overwrite_boot_program_12(image_path, kValidBoot);
    const auto image = jojo::open_iso9660(image_path);
    CHECK(image);
    if (image) {
        std::vector<jojo::GameBackendStage> stages;
        const auto supported = jojo::prepare_game_native_backend(
            jojo::kJojoUsaObservedRevisionId, image.value, root,
            [&](jojo::GameBackendStage s) { stages.push_back(s); });
        CHECK(supported);
        if (supported) {
            CHECK(supported.value.boot_program_hash_hex.size() == 16u);
            CHECK(supported.value.abi_version == jojo::native_backend_abi_version());
            CHECK(!supported.value.program_hash.empty());
            CHECK(supported.value.block_count > 0u);
            CHECK(supported.value.native_block_count + supported.value.fallback_block_count ==
                  supported.value.block_count);
            const std::vector<jojo::GameBackendStage> expected_stages{
                jojo::GameBackendStage::boot_analyzed,
                jojo::GameBackendStage::cache_ready,
                jojo::GameBackendStage::cache_verified};
            CHECK(stages == expected_stages);
            CHECK(fs::is_regular_file(root / "cache/native/compiled_plan.bin"));
        }
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_unknown_revision_cannot_prepare_backend() {
    const auto root = temp_root("wrong-revision");
    const auto image_path = root / "synthetic.iso";
    test_iso::write_image(image_path);
    test_iso::install_dreamcast_ip_metadata(image_path);
    test_iso::overwrite_boot_program_12(image_path, kValidBoot);
    const auto image = jojo::open_iso9660(image_path);
    CHECK(image);
    if (image) {
        const auto wrong_revision = jojo::prepare_game_native_backend(
            "other-revision", image.value, root);
        CHECK(!wrong_revision);
        CHECK(wrong_revision.error == jojo::ErrorCode::backend_unavailable);
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_milcd_encoding_is_a_hard_failure() {
    const auto root = temp_root("milcd");
    const auto image_path = root / "synthetic.iso";
    test_iso::write_image(image_path);
    test_iso::install_dreamcast_ip_metadata(image_path, "CD-ROM1/1");
    test_iso::overwrite_boot_program_12(image_path, kValidBoot);
    const auto image = jojo::open_iso9660(image_path);
    CHECK(image);
    if (image) {
        const auto milcd = jojo::prepare_game_native_backend(
            jojo::kJojoUsaObservedRevisionId, image.value, root);
        CHECK(!milcd);
        CHECK(milcd.error == jojo::ErrorCode::unsupported_format);
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_reachable_unsupported_opcode_is_a_hard_failure() {
    const auto root = temp_root("unsupported-opcode");
    const auto image_path = root / "synthetic.iso";
    test_iso::write_image(image_path);
    test_iso::install_dreamcast_ip_metadata(image_path);
    test_iso::overwrite_boot_program_12(image_path, kUnsupportedBoot);
    const auto image = jojo::open_iso9660(image_path);
    CHECK(image);
    if (image) {
        const auto unsupported = jojo::prepare_game_native_backend(
            jojo::kJojoUsaObservedRevisionId, image.value, root);
        CHECK(!unsupported);
        CHECK(unsupported.error == jojo::ErrorCode::backend_unavailable);
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    test_supported_usa_revision_builds_and_reloads_native_cache();
    test_unknown_revision_cannot_prepare_backend();
    test_milcd_encoding_is_a_hard_failure();
    test_reachable_unsupported_opcode_is_a_hard_failure();
    if (failures) {
        std::cerr << failures << " game-backend assertion(s) failed\n";
        return 1;
    }
    std::cout << "all game-backend assertions passed\n";
    return 0;
}
