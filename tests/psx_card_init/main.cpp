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

    // Exact media frontier: B0(4Bh) StartCARD2. SCPH-1001 ends the
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

    // Next exact media frontier: A0(70h) _bu_init(). The retail call is void;
    // it completes backup-unit setup after InitCARD2/StartCARD2 and returns via RA.
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000a0u);
    runtime.cpu.gpr[9] = 0x70u;
    runtime.cpu.gpr[31] = 0x80044fe0u;
    runtime.cpu.gpr[2] = 0x13579bdfu;
    const auto bu_init = jojo::step_psx_runtime(runtime);
    CHECK(bu_init.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.bios.card_initialized);
    CHECK(runtime.bios.card_started);
    CHECK(runtime.bios.early_card_irq_installed);
    CHECK(runtime.cpu.gpr[2] == 0x13579bdfu);
    CHECK(runtime.cpu.pc == 0x80044fe0u);
    CHECK(runtime.cpu.next_pc == 0x80044fe4u);

    // Exact commercial sequence after _bu_init: eight callback events are opened.
    // SCPH-1001's C0(04h) allocator scans EvCBs from slot zero upward; five slots
    // are already occupied by the BIOS CD-ROM events, so the game receives 5..12.
    constexpr std::uint32_t event_class[] = {
        0xf4000001u, 0xf4000001u, 0xf4000001u, 0xf4000001u,
        0xf0000011u, 0xf0000011u, 0xf0000011u, 0xf0000011u,
    };
    constexpr std::uint32_t event_spec[] = {
        0x00000004u, 0x00008000u, 0x00000100u, 0x00002000u,
        0x00000004u, 0x00008000u, 0x00000100u, 0x00002000u,
    };
    constexpr std::uint32_t event_callback[] = {
        0x80047ad0u, 0x80047ae4u, 0x80047af8u, 0x80047b0cu,
        0x80047b20u, 0x80047b34u, 0x80047b48u, 0x80047b5cu,
    };
    constexpr std::uint32_t open_return[] = {
        0x80047bd0u, 0x80047bf4u, 0x80047c18u, 0x80047c3cu,
        0x80047c60u, 0x80047c84u, 0x80047ca8u, 0x80047cccu,
    };
    std::uint32_t handles[8]{};
    for (std::uint32_t i = 0; i < 8u; ++i) {
        jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
        runtime.cpu.gpr[4] = event_class[i];
        runtime.cpu.gpr[5] = event_spec[i];
        runtime.cpu.gpr[6] = 0x00001000u;
        runtime.cpu.gpr[7] = event_callback[i];
        runtime.cpu.gpr[9] = 0x08u;
        runtime.cpu.gpr[31] = open_return[i];
        const auto opened = jojo::step_psx_runtime(runtime);
        CHECK(opened.reason == jojo::PsxR3000aStepReason::ok);
        handles[i] = runtime.cpu.gpr[2];
        CHECK(handles[i] == 0xf1000005u + i);
        CHECK(runtime.cpu.pc == open_return[i]);
        CHECK(runtime.cpu.next_pc == open_return[i] + 4u);
    }

    // The same routine immediately enables all eight handles through B0(0Ch).
    constexpr std::uint32_t enable_return[] = {
        0x80047ce0u, 0x80047cf0u, 0x80047d00u, 0x80047d10u,
        0x80047d20u, 0x80047d30u, 0x80047d40u, 0x80047d50u,
    };
    for (std::uint32_t i = 0; i < 8u; ++i) {
        jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
        runtime.cpu.gpr[4] = handles[i];
        runtime.cpu.gpr[9] = 0x0cu;
        runtime.cpu.gpr[31] = enable_return[i];
        runtime.cpu.gpr[2] = 0u;
        const auto enabled = jojo::step_psx_runtime(runtime);
        CHECK(enabled.reason == jojo::PsxR3000aStepReason::ok);
        CHECK(runtime.cpu.gpr[2] == 1u);
        CHECK(runtime.cpu.pc == enable_return[i]);
        CHECK(runtime.cpu.next_pc == enable_return[i] + 4u);
    }

    if (failures) return 1;
    std::cout << "PSX card and kernel-event frontier assertions passed\n";
    return 0;
}
