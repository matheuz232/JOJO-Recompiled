#include "core/iso9660.h"
#include "core/revision.h"
#include "iso_fixture.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_file(std::string_view name) {
    auto p = fs::temp_directory_path() / (std::string("jojo_recompiled_") + std::string(name));
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}

static std::uint64_t test_fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

static void test_mount_and_root_listing() {
    const auto path = temp_file("synthetic_fs.iso");
    test_iso::write_image(path);
    const auto mounted = jojo::open_iso9660(path);
    CHECK(mounted);
    if (mounted) {
        const auto entries = jojo::list_iso9660_directory(mounted.value, "/");
        CHECK(entries);
        if (entries) {
            CHECK(entries.value.size() == 2);
            CHECK(entries.value[0].name == "1ST_READ.BIN");
            CHECK(!entries.value[0].is_directory);
            CHECK(entries.value[1].name == "DATA");
            CHECK(entries.value[1].is_directory);
        }
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_nested_lookup_and_bounded_read() {
    const auto path = temp_file("synthetic_nested.iso");
    test_iso::write_image(path);
    const auto mounted = jojo::open_iso9660(path);
    CHECK(mounted);
    if (mounted) {
        const auto data = jojo::list_iso9660_directory(mounted.value, "/data");
        CHECK(data);
        if (data) {
            CHECK(data.value.size() == 1);
            CHECK(data.value[0].name == "ASSET.DAT");
        }
        const auto boot = jojo::read_iso9660_file(mounted.value, "/1st_read.bin");
        CHECK(boot);
        if (boot) CHECK(std::string(boot.value.begin(), boot.value.end()) == "HELLO-SH4!!!");
        const auto asset = jojo::read_iso9660_file(mounted.value, "DATA/asset.dat");
        CHECK(asset);
        if (asset) CHECK(std::string(asset.value.begin(), asset.value.end()) == "ABCDE");
        CHECK(!jojo::read_iso9660_file(mounted.value, "/DATA/../1ST_READ.BIN"));
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_rejects_bad_pvd_and_out_of_bounds_entry() {
    const auto bad_pvd = temp_file("bad_pvd.iso");
    test_iso::write_image(bad_pvd);
    {
        std::fstream io(bad_pvd, std::ios::in | std::ios::out | std::ios::binary);
        io.seekp(static_cast<std::streamoff>(16 * test_iso::sector + 1));
        io.write("XXXXX", 5);
    }
    CHECK(!jojo::open_iso9660(bad_pvd));

    const auto bad_extent = temp_file("bad_extent.iso");
    test_iso::write_image(bad_extent);
    {
        std::fstream io(bad_extent, std::ios::in | std::ios::out | std::ios::binary);
        std::vector<char> bytes(24 * test_iso::sector);
        io.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const std::string needle = "1ST_READ.BIN;1";
        const auto it = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
        CHECK(it != bytes.end());
        if (it != bytes.end()) {
            const auto name_offset = static_cast<std::size_t>(std::distance(bytes.begin(), it));
            const auto record_offset = name_offset - 33;
            io.clear();
            io.seekp(static_cast<std::streamoff>(record_offset + 2));
            const unsigned char invalid_lba[4] = {0xE8, 0x03, 0x00, 0x00};
            io.write(reinterpret_cast<const char*>(invalid_lba), 4);
        }
    }
    const auto mounted = jojo::open_iso9660(bad_extent);
    CHECK(mounted);
    if (mounted) CHECK(!jojo::list_iso9660_directory(mounted.value, "/"));

    std::error_code ec;
    fs::remove(bad_pvd, ec);
    fs::remove(bad_extent, ec);
}

static void test_revision_matcher() {
    const auto path = temp_file("revision.iso");
    test_iso::write_image(path);
    const auto mounted = jojo::open_iso9660(path);
    CHECK(mounted);
    if (mounted) {
        jojo::GameRevisionProfile profile{
            "synthetic-test-revision",
            {
                {"/1ST_READ.BIN", 12, test_fnv1a64("HELLO-SH4!!!")},
                {"/DATA/ASSET.DAT", 5, test_fnv1a64("ABCDE")},
            }
        };
        const auto match = jojo::identify_game_revision(mounted.value, {profile});
        CHECK(match);
        if (match) CHECK(match.value.revision_id == "synthetic-test-revision");

        profile.files[1].fnv1a64 ^= 1;
        const auto unknown = jojo::identify_game_revision(mounted.value, {profile});
        CHECK(!unknown);
        CHECK(unknown.error == jojo::ErrorCode::unknown_revision);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_track_aware_media_mounts_same_iso9660() {
    const auto base = temp_file("track_base.iso");
    const auto bin = temp_file("track_raw.bin");
    const auto gdi = temp_file("track.gdi");
    const auto cue = temp_file("track.cue");
    test_iso::write_image(base);
    test_iso::write_raw2352_from_iso(base, bin, 1);
    {
        std::ofstream out(gdi);
        out << "1\n1 0 4 2352 " << bin.filename().string() << " 0\n";
    }
    {
        std::ofstream out(cue);
        out << "FILE \"" << bin.filename().string() << "\" BINARY\n";
        out << "  TRACK 01 MODE1/2352\n";
        out << "    INDEX 01 00:00:00\n";
    }
    for (const auto& media : {bin, gdi, cue}) {
        const auto mounted = jojo::open_iso9660(media);
        CHECK(mounted);
        if (mounted) {
            const auto boot = jojo::read_iso9660_file(mounted.value, "/1ST_READ.BIN");
            CHECK(boot);
            if (boot) CHECK(std::string(boot.value.begin(), boot.value.end()) == "HELLO-SH4!!!");
        }
    }
    std::error_code ec;
    fs::remove(base, ec); fs::remove(bin, ec); fs::remove(gdi, ec); fs::remove(cue, ec);
}

int main() {
    test_mount_and_root_listing();
    test_nested_lookup_and_bounded_read();
    test_rejects_bad_pvd_and_out_of_bounds_entry();
    test_revision_matcher();
    test_track_aware_media_mounts_same_iso9660();
    if (failures) {
        std::cerr << failures << " ISO9660 assertion(s) failed\n";
        return 1;
    }
    std::cout << "all ISO9660 assertions passed\n";
    return 0;
}
