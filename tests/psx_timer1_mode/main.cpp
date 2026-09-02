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
    CHECK(mode.value == 0x0500u);

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
    CHECK(jojo::psx_bus_read_u16(bus, 0x1f801114u).value == 0x0500u);

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
    // approximately 2152.866 CPU clocks at 33.8688 MHz. The MODE write holds
    // zero through the first selected-clock pulse; the second HBlank is the
    // first one allowed to increment the counter.
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x80010000u);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x1f801114u, 0x00000100u) ==
          jojo::PsxBusAccessReason::ok);
    for (std::uint32_t i = 0; i < 2152u; ++i) {
        CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    }
    CHECK(jojo::psx_bus_read_u16(runtime.bus, 0x1f801110u).value == 0u);
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::psx_bus_read_u16(runtime.bus, 0x1f801110u).value == 0u);
    for (std::uint32_t i = 0; i < 2153u; ++i) {
        CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    }
    CHECK(jojo::psx_bus_read_u16(runtime.bus, 0x1f801110u).value == 1u);

    // The same scanline timing must raise the GPU VBlank interrupt on entry
    // to NTSC line 240. JoJo's frame wait cannot progress if Timer 1 advances
    // while the GPU/VBlank source remains silent.
    for (std::uint32_t line = 2u; line < 240u; ++line) {
        for (std::uint32_t cycle = 0u; cycle < 2153u; ++cycle) {
            CHECK(jojo::step_psx_runtime(runtime).reason ==
                  jojo::PsxR3000aStepReason::ok);
        }
    }
    CHECK((runtime.bus.interrupt_status & 1u) != 0u);

    // The production runtime HLEs the documented SCPH-1001 exception tail:
    // after the IRQ queues run, HookEntryInt restores its setjmp buffer and
    // resumes at saved RA with v0=1. Returning the raw CPU exception here
    // would stop the commercial boot path at its first VBlank.
    jojo::PsxRuntime interrupt_runtime{};
    jojo::reset_psx_r3000a(interrupt_runtime.cpu, 0x80020000u);
    interrupt_runtime.cpu.gpr[8] = 0x12345678u;
    interrupt_runtime.bios.entry_interrupt_hook_installed = true;
    interrupt_runtime.bios.entry_interrupt_hook_address = 0x80001000u;
    const std::uint32_t saved[] = {
        0x80030000u, 0x801ff000u, 0x801fe000u,
        0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
        0x80060000u,
    };
    for (std::uint32_t i = 0; i < 12u; ++i) {
        CHECK(jojo::psx_bus_write_u32(interrupt_runtime.bus,
                                      0x80001000u + i * 4u, saved[i]) ==
              jojo::PsxBusAccessReason::ok);
    }
    interrupt_runtime.bus.interrupt_status = 1u;
    interrupt_runtime.bus.interrupt_mask = 1u;
    interrupt_runtime.cpu.cop0.status = (1u << 10u) | 1u;
    const auto delivered = jojo::step_psx_runtime(interrupt_runtime);
    CHECK(delivered.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(interrupt_runtime.cpu.pc == saved[0]);
    CHECK(interrupt_runtime.cpu.gpr[2] == 1u);
    CHECK(interrupt_runtime.cpu.gpr[29] == saved[1]);
    CHECK(interrupt_runtime.cpu.gpr[30] == saved[2]);
    CHECK(interrupt_runtime.cpu.gpr[16] == saved[3]);
    CHECK(interrupt_runtime.cpu.gpr[23] == saved[10]);
    CHECK(interrupt_runtime.cpu.gpr[28] == saved[11]);
    CHECK((interrupt_runtime.bus.interrupt_status & 1u) == 0u);
    interrupt_runtime.cpu.pc = 0x000000b0u;
    interrupt_runtime.cpu.next_pc = 0x000000b4u;
    interrupt_runtime.cpu.gpr[9] = 0x17u;
    const auto returned = jojo::step_psx_runtime(interrupt_runtime);
    CHECK(returned.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(interrupt_runtime.cpu.pc == 0x80020000u);
    CHECK(interrupt_runtime.cpu.next_pc == 0x80020004u);
    CHECK(interrupt_runtime.cpu.gpr[8] == 0x12345678u);
    CHECK(interrupt_runtime.cpu.cop0.status == ((1u << 10u) | 1u));

    // Timer 1 counts the GPU's physical HBlank signal. Writing MODE resets the
    // counter but must not reset or phase-shift the GPU scanline generator.
    // Rewriting MODE immediately before an already-scheduled HBlank therefore
    // consumes that HBlank as the MODE-write hold; the next HBlank increments.
    jojo::PsxRuntime rewrite_runtime{};
    jojo::reset_psx_r3000a(rewrite_runtime.cpu, 0x80012000u);
    CHECK(jojo::psx_bus_write_u32(rewrite_runtime.bus, 0x1f801114u, 0x00000100u) ==
          jojo::PsxBusAccessReason::ok);
    for (std::uint32_t i = 0; i < 2152u; ++i) {
        CHECK(jojo::step_psx_runtime(rewrite_runtime).reason ==
              jojo::PsxR3000aStepReason::ok);
    }
    CHECK(jojo::psx_bus_read_u16(rewrite_runtime.bus, 0x1f801110u).value == 0u);

    CHECK(jojo::psx_bus_write_u32(rewrite_runtime.bus, 0x1f801114u, 0x00000100u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u16(rewrite_runtime.bus, 0x1f801110u).value == 0u);
    CHECK(jojo::step_psx_runtime(rewrite_runtime).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::psx_bus_read_u16(rewrite_runtime.bus, 0x1f801110u).value == 0u);
    for (std::uint32_t i = 0; i < 2152u; ++i) {
        CHECK(jojo::step_psx_runtime(rewrite_runtime).reason ==
              jojo::PsxR3000aStepReason::ok);
    }
    CHECK(jojo::psx_bus_read_u16(rewrite_runtime.bus, 0x1f801110u).value == 0u);
    CHECK(jojo::step_psx_runtime(rewrite_runtime).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::psx_bus_read_u16(rewrite_runtime.bus, 0x1f801110u).value == 1u);

    if (failures) return 1;
    std::cout << "PSX Timer 1 mode MMIO assertions passed\n";
    return 0;
}
