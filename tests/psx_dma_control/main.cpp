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

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f0u, 0x76543210u) == jojo::PsxBusAccessReason::ok);
    const auto direct = jojo::psx_bus_read_u32(bus, 0x1f8010f0u);
    CHECK(direct.reason == jojo::PsxBusAccessReason::ok);
    CHECK(direct.value == 0x76543210u);

    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c688u);
    cpu.gpr[2] = 0x1f8010f0u;
    cpu.gpr[5] = 0x33333333u;

    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x2b, 2, 5, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003c68cu);
    CHECK(cpu.next_pc == 0x8003c690u);

    const auto stored = jojo::psx_bus_read_u32(bus, 0x1f8010f0u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0x33333333u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u, 0u) == jojo::PsxBusAccessReason::ok);
    const auto dicr_zero = jojo::psx_bus_read_u32(bus, 0x1f8010f4u);
    CHECK(dicr_zero.reason == jojo::PsxBusAccessReason::ok);
    CHECK(dicr_zero.value == 0u);

    bus.dma_interrupt = (1u << 23u) | (1u << 26u);
    const auto dicr_pending = jojo::psx_bus_read_u32(bus, 0x1f8010f4u);
    CHECK(dicr_pending.reason == jojo::PsxBusAccessReason::ok);
    CHECK((dicr_pending.value & (1u << 31u)) != 0u);
    CHECK((dicr_pending.value & (1u << 26u)) != 0u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u,
                                  (1u << 23u) | (1u << 18u) | (1u << 26u)) ==
          jojo::PsxBusAccessReason::ok);
    const auto dicr_acked = jojo::psx_bus_read_u32(bus, 0x1f8010f4u);
    CHECK(dicr_acked.reason == jojo::PsxBusAccessReason::ok);
    CHECK((dicr_acked.value & (1u << 26u)) == 0u);
    CHECK((dicr_acked.value & (1u << 31u)) == 0u);
    CHECK((dicr_acked.value & (1u << 23u)) != 0u);
    CHECK((dicr_acked.value & (1u << 18u)) != 0u);

    const auto gpu_reset = jojo::psx_bus_read_u32(bus, 0x1f801814u);
    CHECK(gpu_reset.reason == jojo::PsxBusAccessReason::ok);
    CHECK(gpu_reset.value == 0x14802000u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801814u, 0x03000000u) ==
          jojo::PsxBusAccessReason::ok);
    const auto display_on = jojo::psx_bus_read_u32(bus, 0x1f801814u);
    CHECK(display_on.reason == jojo::PsxBusAccessReason::ok);
    CHECK(display_on.value == 0x14002000u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801814u, 0x03000001u) ==
          jojo::PsxBusAccessReason::ok);
    const auto display_off = jojo::psx_bus_read_u32(bus, 0x1f801814u);
    CHECK(display_off.reason == jojo::PsxBusAccessReason::ok);
    CHECK(display_off.value == 0x14802000u);

    jojo::reset_psx_r3000a(cpu, 0x800383c0u);
    cpu.gpr[3] = 0x1f8010a8u;
    cpu.gpr[2] = 0x00000401u;
    const auto dma2_setup = jojo::step_psx_r3000a(
        cpu, encode_i(0x2b, 3, 2, 0u), bus);
    CHECK(dma2_setup.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x800383c4u);
    CHECK(cpu.next_pc == 0x800383c8u);

    const auto dma2_chcr = jojo::psx_bus_read_u32(bus, 0x1f8010a8u);
    CHECK(dma2_chcr.reason == jojo::PsxBusAccessReason::ok);
    CHECK(dma2_chcr.value == 0x00000401u);
    CHECK((dma2_chcr.value & (1u << 24u)) == 0u);

    // Real SLUS_010.60 frontier at 800383F4h. Force observable GPU state away
    // from reset first, then GP1(00h) must restore the documented reset state.
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801814u, 0x03000000u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f801814u).value == 0x14002000u);
    jojo::reset_psx_r3000a(cpu, 0x800383f4u);
    cpu.gpr[2] = 0x1f801814u;
    cpu.gpr[0] = 0u;
    const auto gpu_reset_command = jojo::step_psx_r3000a(
        cpu, encode_i(0x2b, 2, 0, 0u), bus);
    CHECK(gpu_reset_command.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x800383f8u);
    CHECK(cpu.next_pc == 0x800383fcu);
    const auto reset_status = jojo::psx_bus_read_u32(bus, 0x1f801814u);
    CHECK(reset_status.reason == jojo::PsxBusAccessReason::ok);
    CHECK(reset_status.value == 0x14802000u);

    if (failures) return 1;
    std::cout << "PSX DMA and GP1 MMIO assertions passed\n";
    return 0;
}
