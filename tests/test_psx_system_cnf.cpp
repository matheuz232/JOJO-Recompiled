#include "core/iso9660.h"
#include "core/psx_boot.h"
#include "core/psx_exe.h"
#include "core/psx_system_cnf.h"
#include "iso_fixture.h"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

static int failures = 0;

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void test_parses_supported_disc_shape() {
    const auto parsed = jojo::parse_psx_system_cnf(
        "BOOT = cdrom:\\SLUS_010.60;1\r\n"
        "TCB = 4\r\n"
        "EVENT = 16\r\n"
        "STACK = 801fff00\r\n");
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.boot_iso_path == "/SLUS_010.60");
        CHECK(parsed.value.tcb == 0x4u);
        CHECK(parsed.value.event == 0x16u);
        CHECK(parsed.value.stack == 0x801fff00u);
    }
}

static void test_accepts_case_insensitive_keys_and_lf() {
    const auto parsed = jojo::parse_psx_system_cnf(
        "boot=CDROM:\\DIR\\GAME.EXE;1\n"
        "tcb=a\n"
        "event=10\n"
        "stack=801ff000\n");
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.boot_iso_path == "/DIR/GAME.EXE");
        CHECK(parsed.value.tcb == 0xau);
        CHECK(parsed.value.event == 0x10u);
        CHECK(parsed.value.stack == 0x801ff000u);
    }
}

static void test_rejects_missing_boot() {
    const auto parsed = jojo::parse_psx_system_cnf("TCB=4\nEVENT=10\nSTACK=801fff00\n");
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_rejects_duplicate_boot() {
    const auto parsed = jojo::parse_psx_system_cnf(
        "BOOT=cdrom:\\A.EXE;1\nBOOT=cdrom:\\B.EXE;1\n");
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_rejects_non_cdrom_boot() {
    const auto parsed = jojo::parse_psx_system_cnf("BOOT=host:\\GAME.EXE\n");
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_rejects_parent_traversal() {
    const auto parsed = jojo::parse_psx_system_cnf("BOOT=cdrom:\\DIR\\..\\GAME.EXE;1\n");
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_rejects_invalid_hex_fields() {
    CHECK(!jojo::parse_psx_system_cnf("BOOT=cdrom:\\GAME.EXE;1\nTCB=nothex\n"));
    CHECK(!jojo::parse_psx_system_cnf("BOOT=cdrom:\\GAME.EXE;1\nEVENT=-1\n"));
    CHECK(!jojo::parse_psx_system_cnf("BOOT=cdrom:\\GAME.EXE;1\nSTACK=100000000\n"));
}

static void test_rejects_embedded_nul() {
    constexpr char bytes[] = "BOOT=cdrom:\\GAME.EXE;1\n\0STACK=801fff00\n";
    const auto parsed = jojo::parse_psx_system_cnf(std::string_view(bytes, sizeof(bytes) - 1));
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void le32(std::vector<std::uint8_t>& file, std::size_t offset, std::uint32_t value) {
    file[offset + 0] = static_cast<std::uint8_t>(value);
    file[offset + 1] = static_cast<std::uint8_t>(value >> 8u);
    file[offset + 2] = static_cast<std::uint8_t>(value >> 16u);
    file[offset + 3] = static_cast<std::uint8_t>(value >> 24u);
}

static std::vector<std::uint8_t> synthetic_psx_exe(std::size_t payload_size = 0x800u) {
    std::vector<std::uint8_t> file(0x800u + payload_size, 0);
    constexpr char magic[] = "PS-X EXE";
    for (std::size_t i = 0; i < sizeof(magic) - 1; ++i) {
        file[i] = static_cast<std::uint8_t>(magic[i]);
    }
    le32(file, 0x10, 0x8001000cu);
    le32(file, 0x14, 0x80020000u);
    le32(file, 0x18, 0x80010000u);
    le32(file, 0x1c, static_cast<std::uint32_t>(payload_size));
    le32(file, 0x20, 0x80030000u);
    le32(file, 0x24, 0x20u);
    le32(file, 0x28, 0x80040000u);
    le32(file, 0x2c, 0x40u);
    le32(file, 0x30, 0x801ffff0u);
    le32(file, 0x34, 0x10u);
    return file;
}

static void test_psx_exe_parses_header_and_payload_contract() {
    const auto file = synthetic_psx_exe();
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.initial_pc == 0x8001000cu);
        CHECK(parsed.value.initial_gp == 0x80020000u);
        CHECK(parsed.value.load_address == 0x80010000u);
        CHECK(parsed.value.payload_size == 0x800u);
        CHECK(parsed.value.data_start == 0x80030000u);
        CHECK(parsed.value.data_size == 0x20u);
        CHECK(parsed.value.bss_start == 0x80040000u);
        CHECK(parsed.value.bss_size == 0x40u);
        CHECK(parsed.value.stack_base == 0x801ffff0u);
        CHECK(parsed.value.stack_offset == 0x10u);
    }
}

static void test_psx_exe_rejects_short_file() {
    const std::vector<std::uint8_t> file(0x7ffu, 0);
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_psx_exe_rejects_bad_magic() {
    auto file = synthetic_psx_exe();
    file[0] = 'X';
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_psx_exe_rejects_non_aligned_payload() {
    auto file = synthetic_psx_exe(0x801u);
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_psx_exe_rejects_declared_size_mismatch() {
    auto file = synthetic_psx_exe();
    le32(file, 0x1c, 0x1000u);
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_psx_exe_rejects_pc_outside_loaded_payload() {
    auto file = synthetic_psx_exe();
    le32(file, 0x10, 0x80010800u);
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static void test_psx_exe_rejects_load_address_overflow() {
    auto file = synthetic_psx_exe();
    le32(file, 0x10, 0xfffffff0u);
    le32(file, 0x18, 0xfffffff0u);
    const auto parsed = jojo::parse_psx_exe(file);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::invalid_installation);
}

static std::filesystem::path temp_iso(std::string_view name) {
    return std::filesystem::temp_directory_path() / std::string(name);
}

static void test_psx_boot_analysis_follows_system_cnf_to_executable() {
    const auto path = temp_iso("jojo_psx_boot_ok.iso");
    test_iso::write_psx_image(path);
    const auto image = jojo::open_iso9660(path);
    CHECK(image);
    if (image) {
        const auto boot = jojo::analyze_psx_boot(image.value);
        CHECK(boot);
        if (boot) {
            CHECK(boot.value.executable_path == "/SLUS_010.60");
            CHECK(boot.value.system.boot_iso_path == "/SLUS_010.60");
            CHECK(boot.value.executable.initial_pc == 0x8001000cu);
            CHECK(boot.value.executable.load_address == 0x80010000u);
            CHECK(boot.value.executable.payload_size == 0x800u);
            CHECK(boot.value.executable.stack_base == 0x801ffff0u);
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void test_psx_boot_analysis_rejects_missing_system_cnf() {
    const auto path = temp_iso("jojo_psx_boot_no_cnf.iso");
    test_iso::PsxImageOptions options{};
    options.include_system_cnf = false;
    test_iso::write_psx_image(path, options);
    const auto image = jojo::open_iso9660(path);
    CHECK(image);
    if (image) CHECK(!jojo::analyze_psx_boot(image.value));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void test_psx_boot_analysis_rejects_missing_boot_file() {
    const auto path = temp_iso("jojo_psx_boot_no_exe.iso");
    test_iso::PsxImageOptions options{};
    options.include_boot_executable = false;
    test_iso::write_psx_image(path, options);
    const auto image = jojo::open_iso9660(path);
    CHECK(image);
    if (image) CHECK(!jojo::analyze_psx_boot(image.value));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void test_psx_boot_analysis_rejects_malformed_system_cnf() {
    const auto path = temp_iso("jojo_psx_boot_bad_cnf.iso");
    test_iso::PsxImageOptions options{};
    options.system_cnf = "BOOT=host:\\SLUS_010.60;1\r\n";
    test_iso::write_psx_image(path, options);
    const auto image = jojo::open_iso9660(path);
    CHECK(image);
    if (image) CHECK(!jojo::analyze_psx_boot(image.value));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void test_psx_boot_analysis_rejects_malformed_executable() {
    const auto path = temp_iso("jojo_psx_boot_bad_exe.iso");
    test_iso::PsxImageOptions options{};
    options.valid_executable_magic = false;
    test_iso::write_psx_image(path, options);
    const auto image = jojo::open_iso9660(path);
    CHECK(image);
    if (image) CHECK(!jojo::analyze_psx_boot(image.value));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

int main() {
    test_parses_supported_disc_shape();
    test_accepts_case_insensitive_keys_and_lf();
    test_rejects_missing_boot();
    test_rejects_duplicate_boot();
    test_rejects_non_cdrom_boot();
    test_rejects_parent_traversal();
    test_rejects_invalid_hex_fields();
    test_rejects_embedded_nul();
    test_psx_exe_parses_header_and_payload_contract();
    test_psx_exe_rejects_short_file();
    test_psx_exe_rejects_bad_magic();
    test_psx_exe_rejects_non_aligned_payload();
    test_psx_exe_rejects_declared_size_mismatch();
    test_psx_exe_rejects_pc_outside_loaded_payload();
    test_psx_exe_rejects_load_address_overflow();
    test_psx_boot_analysis_follows_system_cnf_to_executable();
    test_psx_boot_analysis_rejects_missing_system_cnf();
    test_psx_boot_analysis_rejects_missing_boot_file();
    test_psx_boot_analysis_rejects_malformed_system_cnf();
    test_psx_boot_analysis_rejects_malformed_executable();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 intake parser assertions passed\n";
    return 0;
}
