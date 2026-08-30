#include "core/psx_system_cnf.h"
#include <iostream>
#include <string_view>

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

int main() {
    test_parses_supported_disc_shape();
    test_accepts_case_insensitive_keys_and_lf();
    test_rejects_missing_boot();
    test_rejects_duplicate_boot();
    test_rejects_non_cdrom_boot();
    test_rejects_parent_traversal();
    test_rejects_invalid_hex_fields();
    test_rejects_embedded_nul();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 SYSTEM.CNF assertions passed\n";
    return 0;
}
