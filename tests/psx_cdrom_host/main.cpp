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

int main() {
    jojo::PsxBus bus{};

    // Exact JoJo SLUS_010.60 frontier at 0x8004C55C:
    //   sb $v0,0($v1)   ; $v0=1, $v1=1F801800h
    // The CD host ADDRESS register must select bank 1 rather than fault.
    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8004c55cu);
    cpu.gpr[2] = 1u;
    cpu.gpr[3] = 0x1f801800u;
    const auto select_bank1 = jojo::step_psx_r3000a(
        cpu, encode_i(0x28u, 3u, 2u, 0u), bus);
    CHECK(select_bank1.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8004c560u);

    const auto hsts_bank1 = jojo::psx_bus_read_u8(bus, 0x1f801800u);
    CHECK(hsts_bank1.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hsts_bank1.value & 0x03u) == 1u);
    CHECK((hsts_bank1.value & 0x18u) == 0x18u);
    CHECK((hsts_bank1.value & 0xe4u) == 0u);

    // Bank 1 offset 3 is HINTSTS. Idle has no INTx pending and reserved
    // bits 5-7 read as ones on the retail host interface.
    const auto hintsts_idle = jojo::psx_bus_read_u8(bus, 0x1f801803u);
    CHECK(hintsts_idle.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hintsts_idle.value & 0x07u) == 0u);
    CHECK((hintsts_idle.value & 0xe0u) == 0xe0u);

    // JoJo programs HINTMSK=7 while bank 1 is selected.
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801802u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);

    // HINTMSK is observable by selecting bank 0 and reading offset 3.
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    const auto hintmsk = jojo::psx_bus_read_u8(bus, 0x1f801803u);
    CHECK(hintmsk.reason == jojo::PsxBusAccessReason::ok);
    CHECK(hintmsk.value == 0xe7u);

    // Re-select bank 1 and acknowledge all low interrupt causes. With the
    // idle foundation there are no flags, so HINTSTS must remain INT0.
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801803u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    const auto hintsts_after_ack = jojo::psx_bus_read_u8(bus, 0x1f801803u);
    CHECK(hintsts_after_ack.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hintsts_after_ack.value & 0x07u) == 0u);

    // Return to bank 0 and perform the exact zero HCHPCTL write used by JoJo.
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    const auto hsts_bank0 = jojo::psx_bus_read_u8(bus, 0x1f801800u);
    CHECK(hsts_bank0.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hsts_bank0.value & 0x03u) == 0u);
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801803u, 0u) ==
          jojo::PsxBusAccessReason::ok);

    // Exact next JoJo frontier at 0x8004C628:
    //   sw $v0,0($v1)   ; $v0=00001325h, $v1=1F801020h
    // COM_DELAY stores the four 4-bit common timing values in bits 0-15;
    // the upper half is unused and reads back as zero.
    jojo::PsxR3000aState timing_cpu{};
    jojo::reset_psx_r3000a(timing_cpu, 0x8004c628u);
    timing_cpu.gpr[2] = 0x00001325u;
    timing_cpu.gpr[3] = 0x1f801020u;
    const auto write_com_delay = jojo::step_psx_r3000a(
        timing_cpu, encode_i(0x2bu, 3u, 2u, 0u), bus);
    CHECK(write_com_delay.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(timing_cpu.pc == 0x8004c62cu);

    const auto com_delay = jojo::psx_bus_read_u32(bus, 0x1f801020u);
    CHECK(com_delay.reason == jojo::PsxBusAccessReason::ok);
    CHECK(com_delay.value == 0x00001325u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801020u, 0xabcd5678u) ==
          jojo::PsxBusAccessReason::ok);
    const auto masked_com_delay = jojo::psx_bus_read_u32(bus, 0x1f801020u);
    CHECK(masked_com_delay.reason == jojo::PsxBusAccessReason::ok);
    CHECK(masked_com_delay.value == 0x00005678u);

    if (failures) return 1;
    std::cout << "PSX CD-ROM host interface assertions passed\n";
    return 0;
}
