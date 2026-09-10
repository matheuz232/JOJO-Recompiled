#include "core/ps1_hle_bios.h"
#include "core/r3000a_state.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void test_default_hash_is_deterministic_and_state_changes_hash() {
    jojo::Ps1HleBios first;
    jojo::Ps1HleBios second;
    CHECK(first.diagnostic_state_hash() == second.diagnostic_state_hash());

    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010040u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::a0,
        0x39u,
        0x000000A0u,
        0x00004000u,
        0x00001000u,
        0u,
        0u,
        cpu.gpr[31],
    };
    CHECK(first.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(first.diagnostic_state_hash() != second.diagnostic_state_hash());
}

static void test_a0_initheap_records_state_and_returns_via_ra() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000A0u;
    cpu.next_pc = 0x000000A4u;
    cpu.gpr[31] = 0x80012340u;

    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::a0, 0x39u, cpu.pc,
        0x00004000u, 0x00001000u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.pc == 0x80012340u);
    CHECK(bios.heap_state().has_value());
    CHECK(bios.heap_state()->base == 0x00004000u);
    CHECK(bios.heap_state()->size == 0x00001000u);
}

static void test_a0_remove_aliases_preserve_v0_and_mark_logical_state() {
    for (const std::uint32_t selector : {0x56u, 0x72u}) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.gpr[2] = 0x12345678u;
        cpu.gpr[31] = 0x80010080u;
        const jojo::Ps1HleBiosCall call{
            jojo::Ps1HleBiosDomain::a0, selector, 0xA0u,
            0u, 0u, 0u, 0u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
        CHECK(cpu.gpr[2] == 0x12345678u);
        CHECK(bios.iso9660_removed());
    }
}

int main() {
    test_default_hash_is_deterministic_and_state_changes_hash();
    test_a0_initheap_records_state_and_returns_via_ra();
    test_a0_remove_aliases_preserve_v0_and_mark_logical_state();
    return failures ? 1 : 0;
}
