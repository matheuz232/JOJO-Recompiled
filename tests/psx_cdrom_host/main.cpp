#include "core/psx_bus.h"
#include "core/psx_r3000a.h"
#include "core/psx_runtime.h"
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

    // Exact JoJo SLUS_010.60 frontier at 0x800432D4:
    //   lw $v0,0($a0)   ; $a0=1F801014h SPU_DELAY
    // Retail reset exposes the BIOS-programmed SPU bus delay value. The
    // register is writable and must retain all documented control fields.
    const auto spu_delay_reset = jojo::psx_bus_read_u32(bus, 0x1f801014u);
    CHECK(spu_delay_reset.reason == jojo::PsxBusAccessReason::ok);
    CHECK(spu_delay_reset.value == 0x200931e1u);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801014u, 0x00070777u) ==
          jojo::PsxBusAccessReason::ok);
    const auto spu_delay_programmed = jojo::psx_bus_read_u32(bus, 0x1f801014u);
    CHECK(spu_delay_programmed.reason == jojo::PsxBusAccessReason::ok);
    CHECK(spu_delay_programmed.value == 0x00070777u);

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

    // Exact JoJo frontier reached after COM_DELAY at 0x8004BFEC:
    //   sb $s1,0($v0)   ; $s1=01h Nop, $v0=1F801801h COMMAND
    // With the mounted boot disc the drive is idle with spindle on, so Nop
    // acknowledges with INT3(stat=02h). JoJo has HINTMSK=07h and I_MASK IRQ2
    // enabled; the response must therefore latch I_STAT.bit2 and become a real
    // R3000A external interrupt through COP0 Cause.IP2.
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x8004bfecu);
    runtime.cpu.gpr[2] = 0x1f801801u;
    runtime.cpu.gpr[17] = 0x01u;
    runtime.cpu.cop0.status = 1u | (1u << 10u); // IEc + IM2

    CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x8004bfecu,
                                  encode_i(0x28u, 2u, 17u, 0u)) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x8004bff0u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u16(runtime.bus, jojo::PsxBus::interrupt_mask_address,
                                  static_cast<std::uint16_t>(1u << 2u)) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801802u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);

    const auto nop_command = jojo::step_psx_runtime(runtime);
    CHECK(nop_command.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.pc == 0x8004bff0u);

    const auto hsts_with_result = jojo::psx_bus_read_u8(runtime.bus, 0x1f801800u);
    CHECK(hsts_with_result.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hsts_with_result.value & 0x20u) != 0u); // RSLRRDY

    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    const auto nop_hintsts = jojo::psx_bus_read_u8(runtime.bus, 0x1f801803u);
    CHECK(nop_hintsts.reason == jojo::PsxBusAccessReason::ok);
    CHECK((nop_hintsts.value & 0x07u) == 3u); // INT3 acknowledge response

    const auto cd_irq = jojo::psx_bus_read_u16(runtime.bus, jojo::PsxBus::interrupt_status_address);
    CHECK(cd_irq.reason == jojo::PsxBusAccessReason::ok);
    CHECK((cd_irq.value & (1u << 2u)) != 0u);

    const auto nop_result = jojo::psx_bus_read_u8(runtime.bus, 0x1f801801u);
    CHECK(nop_result.reason == jojo::PsxBusAccessReason::ok);
    CHECK(nop_result.value == 0x02u); // mounted disc, spindle on, shell closed

    const auto hsts_after_result = jojo::psx_bus_read_u8(runtime.bus, 0x1f801800u);
    CHECK(hsts_after_result.reason == jojo::PsxBusAccessReason::ok);
    CHECK((hsts_after_result.value & 0x20u) == 0u);

    const auto delivered_irq = jojo::step_psx_runtime(runtime);
    CHECK(delivered_irq.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(delivered_irq.exception_code == jojo::PsxR3000aExceptionCode::interrupt);
    CHECK(runtime.cpu.cop0.epc == 0x8004bff0u);
    CHECK(runtime.cpu.pc == 0x80000080u);
    CHECK((runtime.cpu.cop0.cause & (1u << 10u)) != 0u);

    // Acknowledge the CD controller separately from the edge-latched I_STAT.
    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801803u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    const auto nop_hintsts_acked = jojo::psx_bus_read_u8(runtime.bus, 0x1f801803u);
    CHECK(nop_hintsts_acked.reason == jojo::PsxBusAccessReason::ok);
    CHECK((nop_hintsts_acked.value & 0x07u) == 0u);
    const auto cd_irq_still_latched = jojo::psx_bus_read_u16(
        runtime.bus, jojo::PsxBus::interrupt_status_address);
    CHECK((cd_irq_still_latched.value & (1u << 2u)) != 0u);

    // Next real SLUS_010.60 frontier: CD command 0Ah Init. It first returns
    // INT3(stat), then completes asynchronously with INT2(stat).
    jojo::PsxBus init_bus{};
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801802u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_cdrom_push_result(init_bus, 0x02u));
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801801u, 0x0au) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u8(init_bus, 0x1f801801u).value == 0x02u);
    CHECK(jojo::psx_bus_read_u8(init_bus, 0x1f801801u).value == 0x02u);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK((jojo::psx_bus_read_u8(init_bus, 0x1f801803u).value & 7u) == 3u);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801803u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    jojo::psx_bus_tick(init_bus, 33'868u);
    CHECK((jojo::psx_bus_read_u8(init_bus, 0x1f801803u).value & 7u) == 0u);
    jojo::psx_bus_tick(init_bus, 1u);
    CHECK((jojo::psx_bus_read_u8(init_bus, 0x1f801803u).value & 7u) == 2u);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u8(init_bus, 0x1f801801u).value == 0x02u);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801803u, 0x07u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);

    init_bus.cdrom_muted = true;
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801801u, 0x0cu) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(!init_bus.cdrom_muted);
    CHECK(jojo::psx_bus_read_u8(init_bus, 0x1f801801u).value == 0x02u);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 1u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK((jojo::psx_bus_read_u8(init_bus, 0x1f801803u).value & 7u) == 3u);

    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 2u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801802u, 0x80u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801803u, 0x00u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801800u, 3u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801801u, 0x80u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801802u, 0x00u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(init_bus, 0x1f801803u, 0x20u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(init_bus.cdrom_volume_left_to_left == 0x80u);
    CHECK(init_bus.cdrom_volume_left_to_right == 0x00u);
    CHECK(init_bus.cdrom_volume_right_to_right == 0x80u);
    CHECK(init_bus.cdrom_volume_right_to_left == 0x00u);

    // Do not silently widen this frontier into arbitrary CD commands.
    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801800u, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u8(runtime.bus, 0x1f801801u, 0x02u) ==
          jojo::PsxBusAccessReason::unmapped);

    if (failures) return 1;
    std::cout << "PSX CD-ROM host interface assertions passed\n";
    return 0;
}
