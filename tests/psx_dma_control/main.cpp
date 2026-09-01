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

    // Exact JoJo SLUS_010.60 frontier at 80043004h programs DMA4/SPU MADR.
    // DMA base addresses are physical 24-bit RAM byte addresses, so the
    // KSEG0 prefix written by the game is discarded on readback.
    jojo::PsxR3000aState dma4_cpu{};
    jojo::reset_psx_r3000a(dma4_cpu, 0x80043004u);
    dma4_cpu.gpr[4] = 0x80062a50u;
    dma4_cpu.gpr[5] = 0x1f8010c0u;
    const auto dma4_madr_write = jojo::step_psx_r3000a(
        dma4_cpu, encode_i(0x2bu, 5u, 4u, 0u), bus);
    CHECK(dma4_madr_write.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(dma4_cpu.pc == 0x80043008u);
    const auto dma4_madr = jojo::psx_bus_read_u32(bus, 0x1f8010c0u);
    CHECK(dma4_madr.reason == jojo::PsxBusAccessReason::ok);
    CHECK(dma4_madr.value == 0x00062a50u);

    // The next observed store programs a 16-word by 16-block DMA4 transfer.
    jojo::reset_psx_r3000a(dma4_cpu, 0x80043020u);
    dma4_cpu.gpr[2] = 0x00100010u;
    dma4_cpu.gpr[3] = 0x1f8010c4u;
    const auto dma4_bcr_write = jojo::step_psx_r3000a(
        dma4_cpu, encode_i(0x2bu, 3u, 2u, 0u), bus);
    CHECK(dma4_bcr_write.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(dma4_cpu.pc == 0x80043024u);
    const auto dma4_bcr = jojo::psx_bus_read_u32(bus, 0x1f8010c4u);
    CHECK(dma4_bcr.reason == jojo::PsxBusAccessReason::ok);
    CHECK(dma4_bcr.value == 0x00100010u);

    // DMA4 request mode must perform the RAM-to-SPU transfer, not merely
    // accept CHCR. SPU transfer addresses are in eight-byte units.
    CHECK(jojo::psx_bus_write_u32(bus, 0x00062a50u, 0x44332211u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x00062a54u, 0x88776655u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801da6u, 2u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010c4u, 0x00010002u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u,
                                  (1u << 23u) | (1u << 20u)) ==
          jojo::PsxBusAccessReason::ok);
    const auto dma4_start = jojo::psx_bus_write_u32(bus, 0x1f8010c8u,
                                                    0x01000201u);
    CHECK(dma4_start == jojo::PsxBusAccessReason::ok);
    CHECK(bus.spu_ram[16u] == 0x11u);
    CHECK(bus.spu_ram[17u] == 0x22u);
    CHECK(bus.spu_ram[22u] == 0x77u);
    CHECK(bus.spu_ram[23u] == 0x88u);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f8010c0u).value == 0x00062a58u);
    CHECK((jojo::psx_bus_read_u32(bus, 0x1f8010c8u).value & (1u << 24u)) == 0u);
    CHECK((jojo::psx_bus_read_u32(bus, 0x1f8010f4u).value & (1u << 28u)) != 0u);
    CHECK((bus.interrupt_status & (1u << 3u)) != 0u);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u, 1u << 28u) ==
          jojo::PsxBusAccessReason::ok);

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

    // Real SLUS_010.60 frontier at 80038758h. GP1(10h).index7 on a v2 retail
    // GPU latches version 2 into GPUREAD at 1F801810h, readable immediately.
    jojo::reset_psx_r3000a(cpu, 0x80038758u);
    cpu.gpr[2] = 0x1f801814u;
    cpu.gpr[3] = 0x10000007u;
    const auto gpu_version_request = jojo::step_psx_r3000a(
        cpu, encode_i(0x2b, 2, 3, 0u), bus);
    CHECK(gpu_version_request.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003875cu);
    CHECK(cpu.next_pc == 0x80038760u);
    const auto gpu_version = jojo::psx_bus_read_u32(bus, 0x1f801810u);
    CHECK(gpu_version.reason == jojo::PsxBusAccessReason::ok);
    CHECK(gpu_version.value == 2u);
    const auto gpu_version_again = jojo::psx_bus_read_u32(bus, 0x1f801810u);
    CHECK(gpu_version_again.reason == jojo::PsxBusAccessReason::ok);
    CHECK(gpu_version_again.value == 2u);

    // GP0(02h) is the first real raster operation required by the commercial
    // command stream. It fills a VRAM rectangle using 15-bit BGR pixels.
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801810u, 0x020000ffu) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801810u, (20u << 16u) | 10u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801810u, (3u << 16u) | 4u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(bus.gpu_vram[20u * 1024u + 10u] == 0x001fu);
    CHECK(bus.gpu_vram[22u * 1024u + 13u] == 0x001fu);
    CHECK(bus.gpu_vram[20u * 1024u + 14u] == 0u);
    CHECK(bus.gpu_vram[23u * 1024u + 10u] == 0u);

    // Real SLUS_010.60 frontier at 80037DA8h selects CPU-to-GP0 DMA.
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801814u, 0x04000002u) ==
          jojo::PsxBusAccessReason::ok);
    const auto gpu_cpu_to_gp0 = jojo::psx_bus_read_u32(bus, 0x1f801814u);
    CHECK(gpu_cpu_to_gp0.reason == jojo::PsxBusAccessReason::ok);
    CHECK(((gpu_cpu_to_gp0.value >> 29u) & 3u) == 2u);
    CHECK((gpu_cpu_to_gp0.value & 0x1fffffffu) ==
          (reset_status.value & 0x1fffffffu));

    // The following instruction programs DMA2/GPU MADR with a KSEG0 pointer.
    jojo::reset_psx_r3000a(cpu, 0x80037db8u);
    cpu.gpr[2] = 0x1f8010a0u;
    cpu.gpr[4] = 0x80095a58u;
    const auto dma2_madr_write = jojo::step_psx_r3000a(
        cpu, encode_i(0x2bu, 2u, 4u, 0u), bus);
    CHECK(dma2_madr_write.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x80037dbcu);
    const auto dma2_madr = jojo::psx_bus_read_u32(bus, 0x1f8010a0u);
    CHECK(dma2_madr.reason == jojo::PsxBusAccessReason::ok);
    CHECK(dma2_madr.value == 0x00095a58u);
    jojo::reset_psx_r3000a(cpu, 0x80037dc8u);
    cpu.gpr[2] = 0x1f8010a4u;
    const auto dma2_bcr_write = jojo::step_psx_r3000a(
        cpu, encode_i(0x2bu, 2u, 0u, 0u), bus);
    CHECK(dma2_bcr_write.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f8010a4u).value == 0u);

    // Linked-list DMA feeds every packet word through GP0 and completes DMA2.
    CHECK(jojo::psx_bus_write_u32(bus, 0x00095a58u, 0x03ffffffu) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x00095a5cu, 0x0200ff00u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x00095a60u, (30u << 16u) | 40u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x00095a64u, (2u << 16u) | 3u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u,
                                  (1u << 23u) | (1u << 18u)) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010a8u, 0x01000401u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(bus.gpu_vram[30u * 1024u + 40u] == 0x03e0u);
    CHECK(bus.gpu_vram[31u * 1024u + 42u] == 0x03e0u);
    CHECK((jojo::psx_bus_read_u32(bus, 0x1f8010a8u).value & (1u << 24u)) == 0u);
    CHECK((jojo::psx_bus_read_u32(bus, 0x1f8010f4u).value & (1u << 26u)) != 0u);
    CHECK((bus.interrupt_status & (1u << 3u)) != 0u);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u, 1u << 26u) ==
          jojo::PsxBusAccessReason::ok);

    // Next real frontier initializes DMA6/OTC CHCR before building an ordering table.
    jojo::reset_psx_r3000a(cpu, 0x8003758cu);
    cpu.gpr[2] = 0x1f8010e8u;
    const auto dma6_chcr_clear = jojo::step_psx_r3000a(
        cpu, encode_i(0x2bu, 2u, 0u, 0u), bus);
    CHECK(dma6_chcr_clear.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x80037590u);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f8010e8u).value == 0u);

    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010e0u, 0x0000100cu) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010e4u, 4u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u,
                                  (1u << 23u) | (1u << 22u)) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010e8u, 0x11000002u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x0000100cu).value == 0x00001008u);
    CHECK(jojo::psx_bus_read_u32(bus, 0x00001008u).value == 0x00001004u);
    CHECK(jojo::psx_bus_read_u32(bus, 0x00001004u).value == 0x00001000u);
    CHECK(jojo::psx_bus_read_u32(bus, 0x00001000u).value == 0x00ffffffu);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f8010e0u).value == 0x00000ffcu);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f8010e4u).value == 0u);
    CHECK((jojo::psx_bus_read_u32(bus, 0x1f8010e8u).value & (1u << 24u)) == 0u);
    CHECK((jojo::psx_bus_read_u32(bus, 0x1f8010f4u).value & (1u << 30u)) != 0u);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8010f4u, 1u << 30u) ==
          jojo::PsxBusAccessReason::ok);

    // Real SLUS_010.60 SPU-init frontier at 8004C3B0h reads the current main
    // volume pair. The SPU register file is 16-bit and reset-silent.
    const auto current_main_left = jojo::psx_bus_read_u16(bus, 0x1f801db8u);
    const auto current_main_right = jojo::psx_bus_read_u16(bus, 0x1f801dbau);
    CHECK(current_main_left.reason == jojo::PsxBusAccessReason::ok);
    CHECK(current_main_right.reason == jojo::PsxBusAccessReason::ok);
    CHECK(current_main_left.value == 0u);
    CHECK(current_main_right.value == 0u);
    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801d80u, 0x1234u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u16(bus, 0x1f801d80u).value == 0x1234u);

    if (failures) return 1;
    std::cout << "PSX DMA and GP1 MMIO assertions passed\n";
    return 0;
}
