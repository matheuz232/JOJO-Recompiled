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
    CHECK((hsts_bank1.value & 0x18u) == 0x18u); // parameter FIFO empty + writable
    CHECK((hsts_bank1.value & 0xe4u) == 0u);    // idle: no ADPCM/result/data/busy

    // Bank 1 offset 3 is HINTSTS. With no command pending there is no INTx,
    // while reserved bits 5-7 read back as ones on retail hardware.
    const auto hintsts_idle = jojo::psx_bus_read_u8(bus, 0x1f801803u);
    CHECK(hintsts_idle.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hintsts_idle.value & 0x07u) == 0u);
    CHECK((hintsts_idle.value & 0xe0u) == 0xe0u);

    // JoJo's cleanup loop writes 07h to HINTMSK and HCLRCTL in bank 1.
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801802u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(bus.cdrom_interrupt_enable == 0x07u);

    bus.cdrom_interrupt_flags = 0x03u;
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801803u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(bus.cdrom_interrupt_flags == 0u);
    CHECK((jojo::psx_bus_read_u8(bus, 0x1f801803u).value & 0x07u) == 0u);

    // Return to bank 0 and perform the exact zero HCHPCTL write used by JoJo.
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK((jojo::psx_bus_read_u8(bus, 0x1f801800u).value & 0x03u) == 0u);
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f801803u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(bus.cdrom_host_control == 0u);

    if (failures) return 1;
    std::cout << "PSX CD-ROM host interface assertions passed\n";
    return 0;
}
