#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

int main() {
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 0u;              // Real SLUS_010.60 InitCARD2(pad_enable=0).
    runtime.cpu.gpr[9] = 0x4au;
    runtime.cpu.gpr[31] = 0x80045068u;    // Real return address.
    runtime.cpu.gpr[2] = 0x2468ace0u;     // Function has no documented return value.

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0x2468ace0u);
    CHECK(runtime.cpu.pc == 0x80045068u);
    CHECK(runtime.cpu.next_pc == 0x8004506cu);
    CHECK(runtime.bios.card_initialized);
    CHECK(!runtime.bios.card_started);
    CHECK(!runtime.bios.card_pad_enabled);
    CHECK(runtime.bios.early_card_irq_installed);

    // Real SLUS_010.60 frontier: B0(57h) GetB0Table, followed by
    // B0[5Bh] + 09C8h patch_card_specific_delay.
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[9] = 0x57u;
    runtime.cpu.gpr[31] = 0x80045750u;
    const auto get_b0 = jojo::step_psx_runtime(runtime);
    CHECK(get_b0.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0x00000874u);
    CHECK(runtime.cpu.pc == 0x80045750u);
    CHECK(runtime.cpu.next_pc == 0x80045754u);

    const auto change_clear_pad = jojo::psx_bus_read_u32(runtime.bus, 0x000009e0u);
    CHECK(change_clear_pad.reason == jojo::PsxBusAccessReason::ok);
    CHECK(change_clear_pad.value == 0x000043d0u);

    constexpr std::uint32_t original[] = {
        0x946f000au, 0x3c080000u, 0x01e2c025u, 0x37190012u, 0xa479000au,
    };
    for (std::uint32_t i = 0; i < 5u; ++i) {
        const auto current = jojo::psx_bus_read_u32(runtime.bus, 0x00004d98u + i * 4u);
        CHECK(current.reason == jojo::PsxBusAccessReason::ok);
        CHECK(current.value == original[i]);
    }

    // The commercial patch must modify ordinary RAM and survive another
    // GetB0Table call; rematerializing the original words would be fake.
    constexpr std::uint32_t patch[] = {
        0x3c08a001u, 0x2508df80u, 0x0100f809u, 0x00000000u, 0x00000000u,
    };
    for (std::uint32_t i = 0; i < 5u; ++i) {
        CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x00004d98u + i * 4u, patch[i]) ==
              jojo::PsxBusAccessReason::ok);
    }
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[9] = 0x57u;
    runtime.cpu.gpr[31] = 0x80045784u;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0x00000874u);
    for (std::uint32_t i = 0; i < 5u; ++i) {
        CHECK(jojo::psx_bus_read_u32(runtime.bus, 0x00004d98u + i * 4u).value == patch[i]);
    }

    // Next exact media frontier: B0(4Bh) StartCARD2. SCPH-1001 ends the
    // routine with "jr ra" / "li v0,1" and starts the shared Pad/Card IRQ path.
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[9] = 0x4bu;
    runtime.cpu.gpr[31] = 0x800450c8u;
    runtime.cpu.gpr[2] = 0xdeadbeefu;
    const auto start = jojo::step_psx_runtime(runtime);
    CHECK(start.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.bios.card_started);
    CHECK(runtime.bios.card_initialized);
    CHECK(runtime.bios.early_card_irq_installed);
    CHECK(runtime.cpu.gpr[2] == 1u);
    CHECK(runtime.cpu.pc == 0x800450c8u);
    CHECK(runtime.cpu.next_pc == 0x800450ccu);

    if (failures) return 1;
    std::cout << "PSX card/GetB0Table/StartCARD2 frontier assertions passed\n";
    return 0;
}
