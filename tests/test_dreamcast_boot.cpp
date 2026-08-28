#include "core/dreamcast_boot.h"
#include "core/iso9660.h"
#include "iso_fixture.h"
#include <algorithm>
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
    auto p = fs::temp_directory_path() / (std::string("jojo_boot_") + std::string(name));
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}

static void write_field(std::fstream& io, std::streamoff offset, std::size_t width, std::string_view text) {
    std::string field(width, ' ');
    std::copy_n(text.begin(), std::min(width, text.size()), field.begin());
    io.seekp(offset);
    io.write(field.data(), static_cast<std::streamsize>(field.size()));
}

static void install_ip_metadata(const fs::path& image,
                                std::string_view boot_filename = "1ST_READ.BIN",
                                std::string_view hardware = "SEGA SEGAKATANA ") {
    std::fstream io(image, std::ios::in | std::ios::out | std::ios::binary);
    write_field(io, 0x000, 16, hardware);
    write_field(io, 0x010, 16, "SEGA ENTERPRISES");
    write_field(io, 0x020, 16, "GD-ROM1/1");
    write_field(io, 0x030, 8, "JUE");
    write_field(io, 0x038, 8, "E000F10");
    write_field(io, 0x040, 10, "T-TEST0001");
    write_field(io, 0x04A, 6, "V1.001");
    write_field(io, 0x050, 16, "20000101");
    write_field(io, 0x060, 16, boot_filename);
    write_field(io, 0x070, 16, "OPENAI TEST");
    write_field(io, 0x080, 128, "JOJO RECOMPILED SYNTHETIC");
}

static void rename_boot_file_same_width(const fs::path& image,
                                        std::string_view from,
                                        std::string_view to) {
    CHECK(from.size() == to.size());
    std::fstream io(image, std::ios::in | std::ios::out | std::ios::binary);
    std::vector<char> bytes(24 * test_iso::sector);
    io.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto it = std::search(bytes.begin(), bytes.end(), from.begin(), from.end());
    CHECK(it != bytes.end());
    if (it == bytes.end()) return;
    const auto offset = static_cast<std::streamoff>(std::distance(bytes.begin(), it));
    io.clear();
    io.seekp(offset);
    io.write(to.data(), static_cast<std::streamsize>(to.size()));
}

static void test_parses_ip_metadata() {
    const auto image = temp_file("metadata.iso");
    test_iso::write_image(image);
    install_ip_metadata(image);
    const auto iso = jojo::open_iso9660(image);
    CHECK(iso);
    if (iso) {
        const auto meta = jojo::read_dreamcast_ip_metadata(iso.value);
        CHECK(meta);
        if (meta) {
            CHECK(meta.value.hardware_id == "SEGA SEGAKATANA");
            CHECK(meta.value.product_number == "T-TEST0001");
            CHECK(meta.value.product_version == "V1.001");
            CHECK(meta.value.area_symbols == "JUE");
            CHECK(meta.value.boot_filename == "1ST_READ.BIN");
            CHECK(meta.value.software_name == "JOJO RECOMPILED SYNTHETIC");
        }
    }
    std::error_code ec;
    fs::remove(image, ec);
}

static void test_discovers_custom_boot_program() {
    const auto image = temp_file("custom_boot.iso");
    test_iso::write_image(image);
    rename_boot_file_same_width(image, "1ST_READ.BIN", "BOOTGAME.BIN");
    install_ip_metadata(image, "BOOTGAME.BIN");
    const auto iso = jojo::open_iso9660(image);
    CHECK(iso);
    if (iso) {
        const auto boot = jojo::read_dreamcast_boot_program(iso.value);
        CHECK(boot);
        if (boot) {
            CHECK(boot.value.metadata.boot_filename == "BOOTGAME.BIN");
            CHECK(std::string(boot.value.bytes.begin(), boot.value.bytes.end()) == "HELLO-SH4!!!");
            CHECK(boot.value.fnv1a64 != 0);
            CHECK(boot.value.hash_hex.size() == 16);
        }
    }
    std::error_code ec;
    fs::remove(image, ec);
}

static void test_rejects_invalid_hardware_signature() {
    const auto image = temp_file("invalid_header.iso");
    test_iso::write_image(image);
    install_ip_metadata(image, "1ST_READ.BIN", "NOT A DREAMCAST");
    const auto iso = jojo::open_iso9660(image);
    CHECK(iso);
    if (iso) {
        const auto meta = jojo::read_dreamcast_ip_metadata(iso.value);
        CHECK(!meta);
        CHECK(meta.error == jojo::ErrorCode::unsupported_format);
    }
    std::error_code ec;
    fs::remove(image, ec);
}

static void test_rejects_unsafe_boot_filename() {
    const auto image = temp_file("unsafe_boot.iso");
    test_iso::write_image(image);
    install_ip_metadata(image, "../EVIL.BIN");
    const auto iso = jojo::open_iso9660(image);
    CHECK(iso);
    if (iso) {
        const auto meta = jojo::read_dreamcast_ip_metadata(iso.value);
        CHECK(!meta);
        CHECK(meta.error == jojo::ErrorCode::invalid_argument);
    }
    std::error_code ec;
    fs::remove(image, ec);
}

static void test_bootstrap_works_through_raw_track_media() {
    const auto cooked = temp_file("raw_source.iso");
    const auto raw = temp_file("raw_source.bin");
    test_iso::write_image(cooked);
    install_ip_metadata(cooked);
    test_iso::write_raw2352_from_iso(cooked, raw, 1);
    const auto iso = jojo::open_iso9660(raw);
    CHECK(iso);
    if (iso) {
        const auto boot = jojo::read_dreamcast_boot_program(iso.value);
        CHECK(boot);
        if (boot) CHECK(boot.value.metadata.product_version == "V1.001");
    }
    std::error_code ec;
    fs::remove(cooked, ec);
    fs::remove(raw, ec);
}

static void test_boot_program_size_limit_is_enforced() {
    const auto image = temp_file("size_limit.iso");
    test_iso::write_image(image);
    install_ip_metadata(image);
    const auto iso = jojo::open_iso9660(image);
    CHECK(iso);
    if (iso) {
        const auto boot = jojo::read_dreamcast_boot_program(iso.value, 8);
        CHECK(!boot);
        CHECK(boot.error == jojo::ErrorCode::invalid_installation);
    }
    std::error_code ec;
    fs::remove(image, ec);
}

int main() {
    test_parses_ip_metadata();
    test_discovers_custom_boot_program();
    test_rejects_invalid_hardware_signature();
    test_rejects_unsafe_boot_filename();
    test_bootstrap_works_through_raw_track_media();
    test_boot_program_size_limit_is_enforced();
    if (failures) {
        std::cerr << failures << " Dreamcast bootstrap assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast bootstrap assertions passed\n";
    return 0;
}
