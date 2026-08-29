#include "core/dreamcast_bus.h"
#include "core/dreamcast_interrupts.h"
#include "core/dreamcast_pvr2.h"
#include "core/dreamcast_system_asic.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::DreamcastExecutableMemory blank_memory() {
    jojo::DreamcastBootProgram program{};
    program.bytes = {0x09u, 0x00u};
    const auto loaded = jojo::load_dreamcast_boot_memory(program);
    CHECK(loaded);
    return loaded ? loaded.value : jojo::DreamcastExecutableMemory{};
}

static void write32(jojo::DreamcastReferenceBus& bus, std::uint32_t address, std::uint32_t value) {
    CHECK(bus.write8(address + 0u, static_cast<std::uint8_t>(value)));
    CHECK(bus.write8(address + 1u, static_cast<std::uint8_t>(value >> 8u)));
    CHECK(bus.write8(address + 2u, static_cast<std::uint8_t>(value >> 16u)));
    CHECK(bus.write8(address + 3u, static_cast<std::uint8_t>(value >> 24u)));
}

static std::uint32_t read32(jojo::DreamcastReferenceBus& bus, std::uint32_t address) {
    const auto b0 = bus.read8(address + 0u);
    const auto b1 = bus.read8(address + 1u);
    const auto b2 = bus.read8(address + 2u);
    const auto b3 = bus.read8(address + 3u);
    CHECK(b0);
    CHECK(b1);
    CHECK(b2);
    CHECK(b3);
    if (!b0 || !b1 || !b2 || !b3) return 0u;
    return static_cast<std::uint32_t>(b0.value)
        | (static_cast<std::uint32_t>(b1.value) << 8u)
        | (static_cast<std::uint32_t>(b2.value) << 16u)
        | (static_cast<std::uint32_t>(b3.value) << 24u);
}

static void test_vblank_begin_reaches_sh4_through_holly_irq_path() {
    auto memory = blank_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastReferenceBus bus(memory);
    bus.attach_device(jojo::DreamcastBusRegion::system_asic, asic);

    // VBlank begin is Holly normal event 3. Route it through IRQ9.
    write32(bus, 0x005F6930u, 1u << 3u);

    jojo::DreamcastPvr2 pvr(asic);
    pvr.vblank_begin();

    CHECK(asic.pending_irq_level().has_value());
    if (asic.pending_irq_level()) CHECK(*asic.pending_irq_level() == 9u);

    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010000u;
    state.vbr = 0x8C000000u;
    state.r[15] = 0x8C00F000u;
    state.sr = 0u;

    const auto delivered = jojo::service_dreamcast_system_irq(asic, state);
    CHECK(delivered);
    if (delivered) CHECK(delivered.value);
    CHECK(state.pc == 0x8C000600u);
    CHECK(state.spc == 0x8C010000u);
    CHECK(state.intevt == 0x000002C0u);
}

static void test_scan_timing_raises_vblank_edges_automatically() {
    auto memory = blank_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastReferenceBus bus(memory);
    bus.attach_device(jojo::DreamcastBusRegion::system_asic, asic);

    write32(bus, 0x005F6930u, (1u << 3u) | (1u << 4u));

    jojo::DreamcastPvr2 pvr(asic);
    pvr.configure_scan_timing({4u, 6u, 4u, 0u});

    pvr.advance_cycles(15u);
    CHECK(!asic.pending_irq_level().has_value());

    pvr.advance_cycles(1u);
    CHECK(asic.pending_irq_level().has_value());
    if (asic.pending_irq_level()) CHECK(*asic.pending_irq_level() == 9u);

    // Acknowledge VBlank-begin, then advance through scanline 5 and frame wrap.
    write32(bus, 0x005F6900u, 1u << 3u);
    CHECK(!asic.pending_irq_level().has_value());

    pvr.advance_cycles(8u);
    CHECK(asic.pending_irq_level().has_value());
    if (asic.pending_irq_level()) CHECK(*asic.pending_irq_level() == 9u);
}

static void test_spg_mmio_programs_scan_timing_and_vblank_edges() {
    auto memory = blank_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastReferenceBus bus(memory);
    bus.attach_device(jojo::DreamcastBusRegion::system_asic, asic);

    jojo::DreamcastPvr2 pvr(asic);
    bus.attach_device(jojo::DreamcastBusRegion::pvr_registers, pvr);

    write32(bus, 0x005F6930u, (1u << 3u) | (1u << 4u));

    // SPG_VBLANK_INT: VBlank-out line 0, VBlank-in line 4.
    write32(bus, 0x005F80CCu, 4u);
    // SPG_LOAD: terminal V counter 5 and H counter 3 => 6 lines x 4 PVR clocks.
    write32(bus, 0x005F80D8u, (5u << 16u) | 3u);

    CHECK(read32(bus, 0x005F80CCu) == 4u);
    CHECK(read32(bus, 0x005F80D8u) == ((5u << 16u) | 3u));

    pvr.advance_cycles(15u);
    CHECK(!asic.pending_irq_level().has_value());
    pvr.advance_cycles(1u);
    CHECK(asic.pending_irq_level().has_value());

    write32(bus, 0x005F6900u, 1u << 3u);
    CHECK(!asic.pending_irq_level().has_value());
    pvr.advance_cycles(8u);
    CHECK(asic.pending_irq_level().has_value());
}

int main() {
    test_vblank_begin_reaches_sh4_through_holly_irq_path();
    test_scan_timing_raises_vblank_edges_automatically();
    test_spg_mmio_programs_scan_timing_and_vblank_edges();
    if (failures) {
        std::cerr << failures << " Dreamcast PVR2 VBlank assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast PVR2 VBlank assertions passed\n";
    return 0;
}
