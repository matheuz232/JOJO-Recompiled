#include "core/conversion.h"
#include "core/game_backend.h"
#include "core/iso9660.h"
#include "core/native_backend.h"
#include "core/runtime.h"
#include "core/version.h"
#include "iso_fixture.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

struct ReadyInstall {
    fs::path root;
    jojo::ConversionManifest manifest;
};

static ReadyInstall make_ready_install(const char* suffix) {
    ReadyInstall fixture{};
    fixture.root = fs::temp_directory_path() / (std::string("jojo-runtime-ready-") + suffix);
    std::error_code ec;
    fs::remove_all(fixture.root, ec);
    fs::create_directories(fixture.root / "data", ec);

    const auto image_path = fixture.root / "synthetic.iso";
    test_iso::write_image(image_path);
    test_iso::install_dreamcast_ip_metadata(image_path);
    test_iso::overwrite_boot_program_12(image_path, kValidBoot);
    const auto image = jojo::open_iso9660(image_path);
    CHECK(image);
    if (!image) return fixture;

    const auto prepared = jojo::prepare_game_native_backend(
        jojo::kJojoUsaObservedRevisionId, image.value, fixture.root);
    CHECK(prepared);
    if (!prepared) return fixture;

    auto& m = fixture.manifest;
    m.manifest_version = "1";
    m.converter_version = jojo::core_version();
    m.source_name = "synthetic.iso";
    m.source_format = "iso";
    m.source_size = fs::file_size(image_path, ec);
    m.hash_hex = "0123456789abcdef";
    m.revision_id = std::string(jojo::kJojoUsaObservedRevisionId);
    m.backend = "native-ready";
    m.boot_program_hash_hex = prepared.value.boot_program_hash_hex;
    m.backend_abi_version = prepared.value.abi_version;
    m.backend_program_hash = prepared.value.program_hash;
    m.backend_block_count = prepared.value.block_count;
    m.backend_native_block_count = prepared.value.native_block_count;
    m.backend_fallback_block_count = prepared.value.fallback_block_count;
    m.backend_native_code_bytes = prepared.value.native_code_bytes;
    CHECK(jojo::save_conversion_manifest_atomic(fixture.root / "game_manifest.ini", m));
    return fixture;
}

static void cleanup(const ReadyInstall& fixture) {
    std::error_code ec;
    fs::remove_all(fixture.root, ec);
}

static void test_valid_verified_backend_bootstraps() {
    const auto fixture = make_ready_install("valid");
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(boot);
    cleanup(fixture);
}

static void test_wrong_revision_is_rejected() {
    auto fixture = make_ready_install("wrong-revision");
    fixture.manifest.revision_id = "other-revision";
    CHECK(jojo::save_conversion_manifest_atomic(
        fixture.root / "game_manifest.ini", fixture.manifest));
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::backend_unavailable);
    cleanup(fixture);
}

static void test_missing_compiled_plan_is_rejected() {
    const auto fixture = make_ready_install("missing-plan");
    std::error_code ec;
    fs::remove(fixture.root / "cache/native/compiled_plan.bin", ec);
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::file_not_found);
    cleanup(fixture);
}

static void test_program_hash_mismatch_is_rejected() {
    auto fixture = make_ready_install("hash-mismatch");
    fixture.manifest.backend_program_hash = "ffffffffffffffff";
    CHECK(jojo::save_conversion_manifest_atomic(
        fixture.root / "game_manifest.ini", fixture.manifest));
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::invalid_installation);
    cleanup(fixture);
}

static void test_abi_mismatch_is_rejected() {
    auto fixture = make_ready_install("abi-mismatch");
    fixture.manifest.backend_abi_version = jojo::native_backend_abi_version() ^ 1u;
    CHECK(jojo::save_conversion_manifest_atomic(
        fixture.root / "game_manifest.ini", fixture.manifest));
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::invalid_installation);
    cleanup(fixture);
}

static void test_block_accounting_mismatch_with_complete_metadata_is_rejected() {
    auto fixture = make_ready_install("count-mismatch");
    fixture.manifest.backend_block_count = *fixture.manifest.backend_block_count + 1u;
    fixture.manifest.backend_fallback_block_count =
        *fixture.manifest.backend_fallback_block_count + 1u;
    CHECK(jojo::has_complete_native_backend_metadata(fixture.manifest));
    CHECK(jojo::save_conversion_manifest_atomic(
        fixture.root / "game_manifest.ini", fixture.manifest));
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::invalid_installation);
    cleanup(fixture);
}

static void test_truncated_compiled_plan_is_rejected() {
    const auto fixture = make_ready_install("truncated-plan");
    {
        std::ofstream out(fixture.root / "cache/native/compiled_plan.bin",
                          std::ios::binary | std::ios::trunc);
        out.write("JOJO", 4);
    }
    const auto boot = jojo::bootstrap_runtime(fixture.root);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::invalid_installation);
    cleanup(fixture);
}

int main() {
    test_valid_verified_backend_bootstraps();
    test_wrong_revision_is_rejected();
    test_missing_compiled_plan_is_rejected();
    test_program_hash_mismatch_is_rejected();
    test_abi_mismatch_is_rejected();
    test_block_accounting_mismatch_with_complete_metadata_is_rejected();
    test_truncated_compiled_plan_is_rejected();
    if (failures) {
        std::cerr << failures << " runtime native-backend assertion(s) failed\n";
        return 1;
    }
    std::cout << "all runtime native-backend assertions passed\n";
    return 0;
}
