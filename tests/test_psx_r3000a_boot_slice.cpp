#include "core/psx_bus.h"
#include "core/psx_r3000a.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

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

static void test_lui_and_addiu_build_boot_addresses() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001000cu);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x0f, 0, 2, 0x8006u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[2] == 0x80060000u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 2, 2, 0x36d8u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[2] == 0x800636d8u);
}

static void test_addiu_sign_extends_immediate() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x1000u);
    state.gpr[4] = 0x1000u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 4, 5, 0xfffcu)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[5] == 0x0ffcu);
}

static void test_sltu_uses_unsigned_comparison() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x2000u);
    state.gpr[2] = 0x80000000u;
    state.gpr[3] = 0xffffffffu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 1, 0, 0x2b)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[1] == 1u);
}

static void test_bne_taken_preserves_delay_slot() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x3000u);
    state.gpr[1] = 1u;
    state.gpr[2] = 0u;

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x05, 1, 2, 0xfffcu)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x3004u);
    CHECK(state.next_pc == 0x2ff4u);

    CHECK(jojo::step_psx_r3000a(state, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x2ff4u);
}

static void test_main_ram_is_two_megabytes_and_zero_initialized() {
    jojo::PsxBus bus{};
    CHECK(bus.ram.size() == 2u * 1024u * 1024u);
    const auto first = jojo::psx_bus_read_u32(bus, 0x00000000u);
    CHECK(first.reason == jojo::PsxBusAccessReason::ok);
    CHECK(first.value == 0u);
}

static void test_ram_kuseg_kseg0_kseg1_and_default_8mb_window_alias_storage() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x00000100u, 0x11223344u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80000100u).value == 0x11223344u);
    CHECK(jojo::psx_bus_read_u32(bus, 0xa0000100u).value == 0x11223344u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x80200104u, 0xaabbccddu) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x00000104u).value == 0xaabbccddu);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80600104u).value == 0xaabbccddu);
}

static void test_ram_access_rejects_unaligned_and_unmapped_addresses() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x80000002u, 1u) ==
          jojo::PsxBusAccessReason::misaligned);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80000002u).reason ==
          jojo::PsxBusAccessReason::misaligned);
    CHECK(jojo::psx_bus_write_u32(bus, 0x80800000u, 1u) ==
          jojo::PsxBusAccessReason::unmapped);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f801000u).reason ==
          jojo::PsxBusAccessReason::unmapped);
}

static void test_sw_uses_signed_offset_and_writes_through_bus() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001000cu);
    state.gpr[2] = 0x800636d8u;
    state.gpr[3] = 0xdeadbeefu;

    const auto result = jojo::step_psx_r3000a(state, encode_i(0x2b, 2, 3, 0xfffcu), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    const auto stored = jojo::psx_bus_read_u32(bus, 0x800636d4u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0xdeadbeefu);
    CHECK(state.pc == 0x80010010u);
}

static void test_sw_reports_memory_fault_without_advancing_pipeline() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001000cu);
    state.gpr[2] = 0x1f801000u;
    state.gpr[3] = 0x12345678u;

    const auto result = jojo::step_psx_r3000a(state, encode_i(0x2b, 2, 3, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::memory_fault);
    CHECK(state.pc == 0x8001000cu);
    CHECK(state.next_pc == 0x80010010u);
}

int main() {
    test_lui_and_addiu_build_boot_addresses();
    test_addiu_sign_extends_immediate();
    test_sltu_uses_unsigned_comparison();
    test_bne_taken_preserves_delay_slot();
    test_main_ram_is_two_megabytes_and_zero_initialized();
    test_ram_kuseg_kseg0_kseg1_and_default_8mb_window_alias_storage();
    test_ram_access_rejects_unaligned_and_unmapped_addresses();
    test_sw_uses_signed_offset_and_writes_through_bus();
    test_sw_reports_memory_fault_without_advancing_pipeline();
    if (failures) return 1;
    std::cout << "R3000A boot integer and RAM/SW assertions passed\n";
    return 0;
}
