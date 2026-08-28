#include "core/disc_media.h"
#include "iso_fixture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_path(const char* name) {
    auto p = fs::temp_directory_path() / (std::string("jojo_media_") + name);
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}

static void test_cooked_iso_source() {
    const auto iso = temp_path("cooked.iso");
    test_iso::write_image(iso);
    const auto source = jojo::open_logical_sector_source(iso);
    CHECK(source);
    if (source) {
        CHECK(source.value.physical_sector_size == 2048);
        CHECK(source.value.user_data_offset == 0);
        CHECK(source.value.logical_sector_count == 24);
        const auto pvd = jojo::read_logical_sectors(source.value, 16, 1);
        CHECK(pvd);
        if (pvd) {
            CHECK(pvd.value.size() == 2048);
            CHECK(pvd.value[0] == 1);
            CHECK(std::string(reinterpret_cast<const char*>(pvd.value.data() + 1), 5) == "CD001");
        }
    }
    std::error_code ec;
    fs::remove(iso, ec);
}

static void test_raw_bin_mode1_source() {
    const auto iso = temp_path("raw_source.iso");
    const auto bin = temp_path("raw_mode1.bin");
    test_iso::write_image(iso);
    test_iso::write_raw2352_from_iso(iso, bin, 1);
    const auto source = jojo::open_logical_sector_source(bin);
    CHECK(source);
    if (source) {
        CHECK(source.value.physical_sector_size == 2352);
        CHECK(source.value.user_data_offset == 16);
        CHECK(source.value.logical_sector_count == 24);
        const auto pvd = jojo::read_logical_sectors(source.value, 16, 1);
        CHECK(pvd);
        if (pvd) CHECK(std::string(reinterpret_cast<const char*>(pvd.value.data() + 1), 5) == "CD001");
    }
    std::error_code ec;
    fs::remove(iso, ec);
    fs::remove(bin, ec);
}

static void test_raw_bin_mode2_form1_source() {
    const auto iso = temp_path("raw_source2.iso");
    const auto bin = temp_path("raw_mode2.bin");
    test_iso::write_image(iso);
    test_iso::write_raw2352_from_iso(iso, bin, 2);
    const auto source = jojo::open_logical_sector_source(bin);
    CHECK(source);
    if (source) {
        CHECK(source.value.physical_sector_size == 2352);
        CHECK(source.value.user_data_offset == 24);
        const auto root = jojo::read_logical_sectors(source.value, 20, 1);
        CHECK(root);
        if (root) CHECK(root.value[0] >= 34);
    }
    std::error_code ec;
    fs::remove(iso, ec);
    fs::remove(bin, ec);
}

static void test_rejects_unrecognized_bin() {
    const auto bin = temp_path("garbage.bin");
    { std::ofstream out(bin, std::ios::binary); out << std::string(100000, 'X'); }
    const auto source = jojo::open_logical_sector_source(bin);
    CHECK(!source);
    CHECK(source.error == jojo::ErrorCode::unsupported_format);
    std::error_code ec;
    fs::remove(bin, ec);
}

static void test_gdi_selects_iso_data_track() {
    const auto iso = temp_path("gdi_source.iso");
    const auto data = temp_path("gdi_track03.bin");
    const auto audio = temp_path("gdi_track02.raw");
    const auto gdi = temp_path("disc.gdi");
    test_iso::write_image(iso);
    test_iso::write_raw2352_from_iso(iso, data, 1);
    { std::ofstream out(audio, std::ios::binary); out << std::string(2352 * 2, '\0'); }
    {
        std::ofstream out(gdi);
        out << "2\n";
        out << "2 0 0 2352 " << audio.filename().string() << " 0\n";
        out << "3 45000 4 2352 " << data.filename().string() << " 0\n";
    }
    const auto source = jojo::open_logical_sector_source(gdi);
    CHECK(source);
    if (source) {
        CHECK(source.value.file_path.filename() == data.filename());
        CHECK(source.value.physical_sector_size == 2352);
        CHECK(source.value.user_data_offset == 16);
        CHECK(source.value.source_format == "gdi");
    }
    std::error_code ec;
    fs::remove(iso, ec); fs::remove(data, ec); fs::remove(audio, ec); fs::remove(gdi, ec);
}

static void test_gdi_rejects_descriptor_path_escape() {
    const auto gdi = temp_path("unsafe.gdi");
    {
        std::ofstream out(gdi);
        out << "1\n";
        out << "1 0 4 2352 ../outside.bin 0\n";
    }
    const auto source = jojo::open_logical_sector_source(gdi);
    CHECK(!source);
    CHECK(source.error == jojo::ErrorCode::invalid_argument);
    std::error_code ec;
    fs::remove(gdi, ec);
}

static void prepend_zero_sector(const fs::path& path, std::size_t bytes) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> original((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::vector<char> zero(bytes, 0);
    out.write(zero.data(), static_cast<std::streamsize>(zero.size()));
    out.write(original.data(), static_cast<std::streamsize>(original.size()));
}

static void test_cue_mode1_uses_index_offset() {
    const auto iso = temp_path("cue_source.iso");
    const auto bin = temp_path("cue_mode1.bin");
    const auto cue = temp_path("disc.cue");
    test_iso::write_image(iso);
    test_iso::write_raw2352_from_iso(iso, bin, 1);
    prepend_zero_sector(bin, 2352);
    {
        std::ofstream out(cue);
        out << "FILE \"" << bin.filename().string() << "\" BINARY\n";
        out << "  TRACK 01 MODE1/2352\n";
        out << "    INDEX 01 00:00:01\n";
    }
    const auto source = jojo::open_logical_sector_source(cue);
    CHECK(source);
    if (source) {
        CHECK(source.value.file_offset == 2352);
        CHECK(source.value.user_data_offset == 16);
        CHECK(source.value.source_format == "cue");
        const auto pvd = jojo::read_logical_sectors(source.value, 16, 1);
        CHECK(pvd);
        if (pvd) CHECK(std::string(reinterpret_cast<const char*>(pvd.value.data() + 1), 5) == "CD001");
    }
    std::error_code ec;
    fs::remove(iso, ec); fs::remove(bin, ec); fs::remove(cue, ec);
}

static void test_cue_mode2_form1_source() {
    const auto iso = temp_path("cue_source2.iso");
    const auto bin = temp_path("cue_mode2.bin");
    const auto cue = temp_path("disc_mode2.cue");
    test_iso::write_image(iso);
    test_iso::write_raw2352_from_iso(iso, bin, 2);
    {
        std::ofstream out(cue);
        out << "FILE \"" << bin.filename().string() << "\" BINARY\n";
        out << "  TRACK 01 MODE2/2352\n";
        out << "    INDEX 01 00:00:00\n";
    }
    const auto source = jojo::open_logical_sector_source(cue);
    CHECK(source);
    if (source) CHECK(source.value.user_data_offset == 24);
    std::error_code ec;
    fs::remove(iso, ec); fs::remove(bin, ec); fs::remove(cue, ec);
}

static void test_cue_rejects_descriptor_path_escape() {
    const auto cue = temp_path("unsafe.cue");
    {
        std::ofstream out(cue);
        out << "FILE \"../outside.bin\" BINARY\n";
        out << "  TRACK 01 MODE1/2352\n";
        out << "    INDEX 01 00:00:00\n";
    }
    const auto source = jojo::open_logical_sector_source(cue);
    CHECK(!source);
    CHECK(source.error == jojo::ErrorCode::invalid_argument);
    std::error_code ec;
    fs::remove(cue, ec);
}

int main() {
    test_cooked_iso_source();
    test_raw_bin_mode1_source();
    test_raw_bin_mode2_form1_source();
    test_rejects_unrecognized_bin();
    test_gdi_selects_iso_data_track();
    test_gdi_rejects_descriptor_path_escape();
    test_cue_mode1_uses_index_offset();
    test_cue_mode2_form1_source();
    test_cue_rejects_descriptor_path_escape();
    if (failures) {
        std::cerr << failures << " media assertion(s) failed\n";
        return 1;
    }
    std::cout << "all media assertions passed\n";
    return 0;
}
