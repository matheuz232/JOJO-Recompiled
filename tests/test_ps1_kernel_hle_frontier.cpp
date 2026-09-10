#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1HleBiosCall call(jojo::Ps1HleBiosDomain domain,
                                 std::uint32_t selector,
                                 const jojo::R3000aState& cpu) {
    return {domain, selector, cpu.pc, cpu.gpr[4], cpu.gpr[5], cpu.gpr[6], cpu.gpr[7], cpu.gpr[31]};
}

static void test_a0_44_flushcache_is_void_for_reference_interpreter() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000A0u;
    cpu.next_pc = 0x000000A4u;
    cpu.gpr[2] = 0x12345678u;
    cpu.gpr[31] = 0x80011000u;

    const auto before_ram = bus.read32(0x00001000u).value;
    CHECK(bios.dispatch(call(jojo::Ps1HleBiosDomain::a0, 0x44u, cpu), cpu, bus).disposition ==
          jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0x12345678u);
    CHECK(cpu.pc == 0x80011000u);
    CHECK(bus.read32(0x00001000u).value == before_ram);
}

static void test_b0_18_resetentryint_materializes_default_jmpbuf() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000B0u;
    cpu.next_pc = 0x000000B4u;
    cpu.gpr[31] = 0x80012000u;

    const auto reset = call(jojo::Ps1HleBiosDomain::b0, 0x18u, cpu);
    CHECK(bios.dispatch(reset, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0x00006CF4u);
    CHECK(cpu.pc == 0x80012000u);
    CHECK(bios.interrupt_hook_address().value_or(0u) == 0x00006CF4u);
    CHECK(bus.read32(0x00006CF4u).value == 0x00000F40u);
    CHECK(bus.read32(0x00006CF8u).value == 0x000085D4u);
    for (std::uint32_t offset = 0x08u; offset < 0x30u; offset += 4u) {
        CHECK(bus.read32(0x00006CF4u + offset).value == 0u);
    }

    CHECK(bus.write32(0x00006CF4u, 0xDEADBEEFu).status == jojo::R3000aBusStatus::ok);
    cpu.pc = 0x000000B0u;
    cpu.next_pc = 0x000000B4u;
    cpu.gpr[31] = 0x80012020u;
    CHECK(bios.dispatch(call(jojo::Ps1HleBiosDomain::b0, 0x18u, cpu), cpu, bus).disposition ==
          jojo::Ps1HleBiosDisposition::handled);
    CHECK(bus.read32(0x00006CF4u).value == 0x00000F40u);
}

static void test_b0_56_getc0table_seeds_once_and_preserves_guest_patch() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000B0u;
    cpu.next_pc = 0x000000B4u;
    cpu.gpr[31] = 0x80013000u;

    CHECK(bios.dispatch(call(jojo::Ps1HleBiosDomain::b0, 0x56u, cpu), cpu, bus).disposition ==
          jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0x00000674u);
    CHECK(bus.read32(0x00000674u + 6u * 4u).value == 0x00000C80u);
    CHECK(bus.write32(0x00000674u + 6u * 4u, 0x80012340u).status == jojo::R3000aBusStatus::ok);

    cpu.pc = 0x000000B0u;
    cpu.next_pc = 0x000000B4u;
    cpu.gpr[31] = 0x80013020u;
    CHECK(bios.dispatch(call(jojo::Ps1HleBiosDomain::b0, 0x56u, cpu), cpu, bus).disposition ==
          jojo::Ps1HleBiosDisposition::handled);
    CHECK(bus.read32(0x00000674u + 6u * 4u).value == 0x80012340u);
}

int main() {
    test_a0_44_flushcache_is_void_for_reference_interpreter();
    test_b0_18_resetentryint_materializes_default_jmpbuf();
    test_b0_56_getc0table_seeds_once_and_preserves_guest_patch();
    return failures ? 1 : 0;
}
