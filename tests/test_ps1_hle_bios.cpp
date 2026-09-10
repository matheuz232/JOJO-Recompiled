#include "core/ps1_hle_bios.h"
#include "core/r3000a_state.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static constexpr std::uint32_t kSafeA0ReturnZero[] = {
    0x57u, 0x58u, 0x59u, 0x5Au,
    0x73u, 0x74u, 0x75u, 0x76u, 0x77u,
    0x79u, 0x7Au, 0x7Bu, 0x7Du,
    0x7Fu, 0x80u,
    0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u, 0x89u,
    0x8Au, 0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu,
    0xB0u, 0xB1u, 0xB3u,
};

static constexpr std::uint32_t kSafeC0ReturnZero[] = {
    0x0Eu, 0x0Fu, 0x10u, 0x11u, 0x14u,
};

static void test_default_hash_is_deterministic_and_state_changes_hash() {
    jojo::Ps1HleBios first;
    jojo::Ps1HleBios second;
    CHECK(first.diagnostic_state_hash() == second.diagnostic_state_hash());
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010040u;
    const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::a0, 0x39u, 0xA0u,
        0x4000u, 0x1000u, 0u, 0u, cpu.gpr[31]};
    CHECK(first.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(first.diagnostic_state_hash() != second.diagnostic_state_hash());
}

static void test_a0_initheap_records_state_and_returns_via_ra() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0xA0u;
    cpu.next_pc = 0xA4u;
    cpu.gpr[31] = 0x80012340u;
    const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::a0, 0x39u, cpu.pc,
        0x4000u, 0x1000u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.pc == 0x80012340u);
    CHECK(bios.heap_state().has_value());
    CHECK(bios.heap_state()->base == 0x4000u);
    CHECK(bios.heap_state()->size == 0x1000u);
}

static void test_a0_remove_aliases_preserve_v0_and_mark_logical_state() {
    for (const std::uint32_t selector : {0x56u, 0x72u}) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.gpr[2] = 0x12345678u;
        cpu.gpr[31] = 0x80010080u;
        const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::a0, selector, 0xA0u,
            0u, 0u, 0u, 0u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
        CHECK(cpu.gpr[2] == 0x12345678u);
        CHECK(bios.iso9660_removed());
    }
}

static void test_b0_hookentryint_records_pointer() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010100u;
    const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::b0, 0x19u, 0xB0u,
        0x800616F0u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(bios.interrupt_hook_address().has_value());
    CHECK(bios.interrupt_hook_address().value_or(0u) == 0x800616F0u);
}

static void test_b0_changeclearpad_records_flag() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010100u;
    const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::b0, 0x5Bu, 0xB0u,
        0u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(bios.pad_card_auto_ack_enabled().has_value());
    CHECK(!bios.pad_card_auto_ack_enabled().value_or(true));
}

static void test_c0_changeclearrcnt_returns_previous_flag() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010100u;
    const jojo::Ps1HleBiosCall disable{jojo::Ps1HleBiosDomain::c0, 0x0Au, 0xC0u,
        3u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(disable, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0u);
    CHECK(bios.root_counter_auto_ack_enabled(3u).has_value());
    CHECK(!bios.root_counter_auto_ack_enabled(3u).value_or(true));
    cpu.gpr[31] = 0x80010120u;
    const jojo::Ps1HleBiosCall enable{jojo::Ps1HleBiosDomain::c0, 0x0Au, 0xC0u,
        3u, 1u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(enable, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0u);
    CHECK(bios.root_counter_auto_ack_enabled(3u).value_or(false));
}

static void test_sys00_preserves_registers_and_advances_instruction() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x80010000u;
    cpu.next_pc = 0x80010004u;
    cpu.gpr[2] = 0x12345678u;
    const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::sys, 0u, cpu.pc,
        0u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.pc == 0x80010004u);
    CHECK(cpu.next_pc == 0x80010008u);
    CHECK(cpu.gpr[2] == 0x12345678u);
}

static void test_sys01_sys02_match_critical_section_contract() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x80010000u;
    cpu.next_pc = 0x80010004u;
    cpu.cop0.status = 0x00000401u;
    const jojo::Ps1HleBiosCall enter{jojo::Ps1HleBiosDomain::sys, 1u, cpu.pc,
        1u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(enter, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 1u);
    CHECK((cpu.cop0.status & 0x00000401u) == 0u);

    cpu.next_pc = cpu.pc + 4u;
    const jojo::Ps1HleBiosCall exit{jojo::Ps1HleBiosDomain::sys, 2u, cpu.pc,
        2u, 0u, 0u, 0u, cpu.gpr[31]};
    const auto preserved_v0 = cpu.gpr[2];
    CHECK(bios.dispatch(exit, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == preserved_v0);
    CHECK((cpu.cop0.status & 0x00000401u) == 0x00000401u);
}

static void test_sys03_is_unsupported_without_mutation() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x80010000u;
    cpu.next_pc = 0x80010004u;
    cpu.gpr[2] = 0x11223344u;
    cpu.cop0.status = 0x00000401u;
    const auto before = cpu;
    const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::sys, 3u, cpu.pc,
        3u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::unsupported);
    CHECK(cpu.pc == before.pc);
    CHECK(cpu.next_pc == before.next_pc);
    CHECK(cpu.gpr[2] == before.gpr[2]);
    CHECK(cpu.cop0.status == before.cop0.status);
}

static void test_safe_documented_return_zero_selectors() {
    for (const auto selector : kSafeA0ReturnZero) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.pc = 0x000000A0u;
        cpu.next_pc = 0x000000A4u;
        cpu.gpr[2] = 0xDEADBEEFu;
        cpu.gpr[31] = 0x80012000u;
        const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::a0, selector, cpu.pc,
            0x11u, 0x22u, 0x33u, 0x44u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
        CHECK(cpu.gpr[2] == 0u);
        CHECK(cpu.pc == 0x80012000u);
    }
    for (const auto selector : kSafeC0ReturnZero) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.pc = 0x000000C0u;
        cpu.next_pc = 0x000000C4u;
        cpu.gpr[2] = 0xDEADBEEFu;
        cpu.gpr[31] = 0x80013000u;
        const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::c0, selector, cpu.pc,
            0x11u, 0x22u, 0x33u, 0x44u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
        CHECK(cpu.gpr[2] == 0u);
        CHECK(cpu.pc == 0x80013000u);
    }
}

static void test_nearby_excluded_selectors_remain_unsupported_without_mutation() {
    static constexpr std::uint32_t excluded_a0[] = {0x5Bu, 0x78u, 0x7Cu, 0x7Eu, 0x81u, 0xB2u};
    static constexpr std::uint32_t excluded_c0[] = {0x12u, 0x13u, 0x15u};

    for (const auto selector : excluded_a0) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.pc = 0xA0u;
        cpu.next_pc = 0xA4u;
        cpu.gpr[2] = 0xCAFEBABEu;
        cpu.gpr[31] = 0x80014000u;
        const auto cpu_before = cpu;
        const auto hash_before = bios.diagnostic_state_hash();
        const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::a0, selector, cpu.pc,
            1u, 2u, 3u, 4u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::unsupported);
        CHECK(cpu.pc == cpu_before.pc);
        CHECK(cpu.next_pc == cpu_before.next_pc);
        CHECK(cpu.gpr[2] == cpu_before.gpr[2]);
        CHECK(bios.diagnostic_state_hash() == hash_before);
    }
    for (const auto selector : excluded_c0) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.pc = 0xC0u;
        cpu.next_pc = 0xC4u;
        cpu.gpr[2] = 0xCAFEBABEu;
        cpu.gpr[31] = 0x80015000u;
        const auto cpu_before = cpu;
        const auto hash_before = bios.diagnostic_state_hash();
        const jojo::Ps1HleBiosCall call{jojo::Ps1HleBiosDomain::c0, selector, cpu.pc,
            1u, 2u, 3u, 4u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::unsupported);
        CHECK(cpu.pc == cpu_before.pc);
        CHECK(cpu.next_pc == cpu_before.next_pc);
        CHECK(cpu.gpr[2] == cpu_before.gpr[2]);
        CHECK(bios.diagnostic_state_hash() == hash_before);
    }
}

int main() {
    test_default_hash_is_deterministic_and_state_changes_hash();
    test_a0_initheap_records_state_and_returns_via_ra();
    test_a0_remove_aliases_preserve_v0_and_mark_logical_state();
    test_b0_hookentryint_records_pointer();
    test_b0_changeclearpad_records_flag();
    test_c0_changeclearrcnt_returns_previous_flag();
    test_sys00_preserves_registers_and_advances_instruction();
    test_sys01_sys02_match_critical_section_contract();
    test_sys03_is_unsupported_without_mutation();
    test_safe_documented_return_zero_selectors();
    test_nearby_excluded_selectors_remain_unsupported_without_mutation();
    return failures ? 1 : 0;
}
