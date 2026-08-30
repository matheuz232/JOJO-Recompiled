#include "core/psx_bus.h"
#include "core/psx_r3000a.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

static void test_main_ram_is_two_megabytes_and_zero_initialized() {
    jojo::PsxBus bus{};
    CHECK(bus.ram.size() == 2u * 1024u * 1024u);
    const auto first = jojo::psx_bus_read_u32(bus, 0x00000000u);
    CHECK(first.reason == jojo::PsxBusAccessReason::ok);
    CHECK(first.value == 0u);
}

static void test_ram_kuseg_kseg0_kseg1_alias_the_same_storage() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x00000100u, 0x11223344u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80000100u).value == 0x11223344u);
    CHECK(jojo::psx_bus_read_u32(bus, 0xa0000100u).value == 0x11223344u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x80000104u, 0xaabbccddu) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x00000104u).value == 0xaabbccddu);
}

static void test_ram_access_rejects_unaligned_and_unmapped_addresses() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x80000002u, 1u) ==
          jojo::PsxBusAccessReason::misaligned);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80000002u).reason ==
          jojo::PsxBusAccessReason::misaligned);
    CHECK(jojo::psx_bus_write_u32(bus, 0x80200000u, 1u) ==
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
    test_main_ram_is_two_megabytes_and_zero_initialized();
    test_ram_kuseg_kseg0_kseg1_alias_the_same_storage();
    test_ram_access_rejects_unaligned_and_unmapped_addresses();
    test_sw_uses_signed_offset_and_writes_through_bus();
    test_sw_reports_memory_fault_without_advancing_pipeline();
    if (failures) return 1;
    std::cout << "PS1 RAM bus and SW contract passed\n";
    return 0;
}
