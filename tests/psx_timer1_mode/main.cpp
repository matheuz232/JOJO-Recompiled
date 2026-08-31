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

    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801110u, 0x1234u) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801114u, 0x00000100u) == jojo::PsxBusAccessReason::ok);

    const auto mode = jojo::psx_bus_read_u16(bus, 0x1f801114u);
    CHECK(mode.reason == jojo::PsxBusAccessReason::ok);
    CHECK(mode.value == 0x0100u);

    const auto counter = jojo::psx_bus_read_u16(bus, 0x1f801110u);
    CHECK(counter.reason == jojo::PsxBusAccessReason::ok);
    CHECK(counter.value == 0u);

    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003cc4cu);
    cpu.gpr[3] = 0x1f801114u;
    cpu.gpr[2] = 0x00000100u;

    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x2b, 3, 2, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003cc50u);
    CHECK(cpu.next_pc == 0x8003cc54u);
    CHECK(jojo::psx_bus_read_u16(bus, 0x1f801114u).value == 0x0100u);

    // Real SLUS_010.60 frontier at 8003C234h reads Timer 1 current with LW.
    // Hardware defines only bits 0-15; bits 16-31 are garbage, so assert only
    // the architecturally meaningful low half after the load-delay slot.
    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801110u, 0x3456u) == jojo::PsxBusAccessReason::ok);
    jojo::reset_psx_r3000a(cpu, 0x8003c234u);
    cpu.gpr[5] = 0x1f801110u;
    cpu.gpr[2] = 0xaaaaaaaau;
    const auto timer1_lw = jojo::step_psx_r3000a(
        cpu, encode_i(0x23, 5, 2, 0u), bus);
    CHECK(timer1_lw.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[2] == 0xaaaaaaaau);
    CHECK(jojo::step_psx_r3000a(cpu, 0u, bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK((cpu.gpr[2] & 0xffffu) == 0x3456u);

    // JoJo configures Timer 1 mode=0100h, selecting HBlank as its clock.
    // On an NTSC PS1, 3413 video clocks per scanline at 53.693175 MHz are
    // approximately 2152.866 CPU clocks at 33.8688 MHz. Runtime execution must
    // therefore leave the counter at zero through 2152 one-cycle instructions
    // and increment it on the next cycle instead of freezing forever.
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x80010000u);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x1f801114u, 0x00000100u) ==
          jojo::PsxBusAccessReason::ok);
    for (std::uint32_t i = 0; i < 2152u; ++i) {
        CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    }
    CHECK(jojo::psx_bus_read_u16(runtime.bus, 0x1f801110u).value == 0u);
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::psx_bus_read_u16(runtime.bus, 0x1f801110u).value == 1u);

    if (failures) return 1;
    std::cout << "PSX Timer 1 mode MMIO assertions passed\n";
    return 0;
}
