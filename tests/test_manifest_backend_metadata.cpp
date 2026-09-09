#include "core/conversion.h"
#include "core/game_backend.h"
#include "core/version.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_file(std::string_view suffix) {
    auto path = fs::temp_directory_path() / (std::string("jojo-manifest-backend-") + std::string(suffix));
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path.string() + ".tmp", ec);
    return path;
}

static jojo::ConversionManifest ordinary_manifest() {
    jojo::ConversionManifest m{};
    m.manifest_version = "1";
    m.converter_version = jojo::core_version();
    m.source_name = "owned.iso";
    m.source_format = "iso";
    m.source_size = 1234u;
    m.hash_hex = "0123456789abcdef";
    m.revision_id = "synthetic-test-revision";
    return m;
}

static void test_pending_manifest_remains_backward_compatible() {
    const auto path = temp_file("pending.ini");
    auto m = ordinary_manifest();
    CHECK(jojo::save_conversion_manifest_atomic(path, m));
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(!jojo::has_complete_native_backend_metadata(loaded.value));
        CHECK(loaded.value.backend == "pending-game-specific-recompiler");
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_native_ready_manifest_round_trips_complete_backend_metadata() {
    const auto path = temp_file("ready.ini");
    auto m = ordinary_manifest();
    m.revision_id = std::string(jojo::kJojoUsaObservedRevisionId);
    m.backend = "native-ready";
    m.boot_program_hash_hex = "1111111111111111";
    m.backend_abi_version = 0x00020001u;
    m.backend_program_hash = "2222222222222222";
    m.backend_block_count = 4u;
    m.backend_native_block_count = 3u;
    m.backend_fallback_block_count = 1u;
    m.backend_native_code_bytes = 64u;
    CHECK(jojo::save_conversion_manifest_atomic(path, m));
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(jojo::has_complete_native_backend_metadata(loaded.value));
        CHECK(loaded.value.boot_program_hash_hex == "1111111111111111");
        CHECK(loaded.value.backend_abi_version == 0x00020001u);
        CHECK(loaded.value.backend_program_hash == "2222222222222222");
        CHECK(loaded.value.backend_block_count == 4u);
        CHECK(loaded.value.backend_native_block_count == 3u);
        CHECK(loaded.value.backend_fallback_block_count == 1u);
        CHECK(loaded.value.backend_native_code_bytes == 64u);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void write_ready_prefix(std::ofstream& out) {
    out << "manifest_version=1\n";
    out << "converter_version=0.2.0-test\n";
    out << "source_name=owned.iso\n";
    out << "source_format=iso\n";
    out << "source_size=1234\n";
    out << "hash_fnv1a64=0123456789abcdef\n";
    out << "revision_id=" << jojo::kJojoUsaObservedRevisionId << "\n";
    out << "backend=native-ready\n";
    out << "boot_program_hash_fnv1a64=1111111111111111\n";
}

static void test_native_ready_manifest_rejects_missing_backend_program_hash() {
    const auto path = temp_file("missing-hash.ini");
    {
        std::ofstream out(path, std::ios::trunc);
        write_ready_prefix(out);
        out << "backend_abi_version=131073\n";
        out << "backend_block_count=4\n";
        out << "backend_native_block_count=3\n";
        out << "backend_fallback_block_count=1\n";
        out << "backend_native_code_bytes=64\n";
    }
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(!loaded);
    CHECK(loaded.error == jojo::ErrorCode::invalid_installation);
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_native_ready_manifest_rejects_malformed_backend_abi() {
    const auto path = temp_file("bad-abi.ini");
    {
        std::ofstream out(path, std::ios::trunc);
        write_ready_prefix(out);
        out << "backend_abi_version=not-a-number\n";
        out << "backend_program_hash=2222222222222222\n";
        out << "backend_block_count=4\n";
        out << "backend_native_block_count=3\n";
        out << "backend_fallback_block_count=1\n";
        out << "backend_native_code_bytes=64\n";
    }
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(!loaded);
    CHECK(loaded.error == jojo::ErrorCode::invalid_installation);
    std::error_code ec;
    fs::remove(path, ec);
}

int main() {
    test_pending_manifest_remains_backward_compatible();
    test_native_ready_manifest_round_trips_complete_backend_metadata();
    test_native_ready_manifest_rejects_missing_backend_program_hash();
    test_native_ready_manifest_rejects_malformed_backend_abi();
    if (failures) {
        std::cerr << failures << " manifest-backend assertion(s) failed\n";
        return 1;
    }
    std::cout << "all manifest-backend assertions passed\n";
    return 0;
}
