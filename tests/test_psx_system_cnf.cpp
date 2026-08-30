#include "core/conversion.h"
#include "core/iso9660.h"
#include "core/psx_boot.h"
#include "core/psx_exe.h"
#include "core/psx_r3000a.h"
#include "core/psx_revision.h"
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

static void test_supported_psx_revision_profile_is_exact() {
    const auto& profiles = jojo::supported_psx_game_revision_profiles();
    CHECK(profiles.size() == 1u);
    if (profiles.size() == 1u) {
        const auto& profile = profiles.front();
        CHECK(profile.revision_id == "ps1-usa-slus-01060");
        CHECK(profile.files.size() == 2u);
        if (profile.files.size() == 2u) {
            CHECK(profile.files[0].path == "/SYSTEM.CNF");
            CHECK(profile.files[0].size_bytes == 68u);
            CHECK(profile.files[0].fnv1a64 == 0x1eb36f6335bbf54aull);
            CHECK(profile.files[1].path == "/SLUS_010.60");
            CHECK(profile.files[1].size_bytes == 565248u);
            CHECK(profile.files[1].fnv1a64 == 0xb84be235e572adccull);
        }
    }
}

static void test_default_conversion_rejects_filename_correct_but_fingerprint_wrong_image() {
    const auto source = temp_iso("jojo_psx_wrong_revision.iso");
    const auto install = std::filesystem::temp_directory_path() / "jojo_psx_wrong_revision_install";
    std::error_code ec;
    std::filesystem::remove_all(install, ec);
    test_iso::write_psx_image(source);

    const auto converted = jojo::convert_image(source, install);
    CHECK(!converted);
    if (!converted) {
        CHECK(converted.error == jojo::ErrorCode::unknown_revision);
        CHECK(converted.detail.find("fingerprint mismatch") != std::string::npos);
    }
    CHECK(!std::filesystem::exists(install / "game_manifest.ini"));

    std::filesystem::remove(source, ec);
    std::filesystem::remove_all(install, ec);
}

static std::uint32_t encode_r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                              std::uint8_t shamt, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(shamt) << 6u) |
           funct;
}

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

static std::uint32_t encode_j(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) | ((target >> 2u) & 0x03ffffffu);
}

static void test_r3000a_reset_and_zero_register_invariant() {
    jojo::PsxR3000aState state{};
    state.gpr.fill(0xffffffffu);
    jojo::reset_psx_r3000a(state, 0x8001000cu);
    CHECK(state.pc == 0x8001000cu);
    CHECK(state.next_pc == 0x80010010u);
    for (const auto value : state.gpr) CHECK(value == 0u);

    state.gpr[1] = 7u;
    state.gpr[2] = 9u;
    const auto result = jojo::step_psx_r3000a(state, encode_r(1, 2, 0, 0, 0x21));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[0] == 0u);
}

static void test_r3000a_addu_subu_wrap_without_overflow_exception() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010000u);
    state.gpr[1] = 0xffffffffu;
    state.gpr[2] = 2u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(1, 2, 3, 0, 0x21)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 1u);

    state.gpr[4] = 1u;
    state.gpr[5] = 2u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(4, 5, 6, 0, 0x23)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[6] == 0xffffffffu);
}

static void test_r3000a_taken_branch_executes_delay_slot_before_target() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x1000u);
    state.gpr[1] = 7u;
    state.gpr[2] = 7u;

    const auto branch = jojo::step_psx_r3000a(state, encode_i(0x04, 1, 2, 2));
    CHECK(branch.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x1004u);
    CHECK(state.next_pc == 0x100cu);

    const auto delay = jojo::step_psx_r3000a(state, 0u);
    CHECK(delay.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x100cu);
    CHECK(state.next_pc == 0x1010u);
}

static void test_r3000a_jal_links_after_delay_slot() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010000u);
    const auto result = jojo::step_psx_r3000a(state, encode_j(0x03, 0x80011000u));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[31] == 0x80010008u);
    CHECK(state.pc == 0x80010004u);
    CHECK(state.next_pc == 0x80011000u);
}

static void test_r3000a_reports_unsupported_opcode() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010000u);
    const auto result = jojo::step_psx_r3000a(state, 0xfc000000u);
    CHECK(result.reason == jojo::PsxR3000aStepReason::unsupported_instruction);
    CHECK(result.instruction == 0xfc000000u);
    CHECK(result.instruction_pc == 0x80010000u);
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
    test_supported_psx_revision_profile_is_exact();
    test_default_conversion_rejects_filename_correct_but_fingerprint_wrong_image();
    test_r3000a_reset_and_zero_register_invariant();
    test_r3000a_addu_subu_wrap_without_overflow_exception();
    test_r3000a_taken_branch_executes_delay_slot_before_target();
    test_r3000a_jal_links_after_delay_slot();
    test_r3000a_reports_unsupported_opcode();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 intake and R3000A foundation assertions passed\n";
    return 0;
}
