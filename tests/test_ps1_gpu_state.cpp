#include "core/ps1_gpu_state.h"
#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"
#include "core/r3000a_state.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void test_gp1_reset_state_and_supported_control_commands() {
    jojo::Ps1GpuState gpu;
    CHECK(gpu.gpu_stat() == 0x14802000u);
    CHECK(gpu.gp1_command_count() == 0u);

    CHECK(gpu.write_gp1(0x03000000u));
    CHECK(!gpu.display_disabled());
    CHECK(gpu.write_gp1(0x04000002u));
    CHECK(gpu.dma_direction() == 2u);

    CHECK(gpu.write_gp1(0x05012345u));
    CHECK(gpu.display_vram_x() == (0x012345u & 0x3FFu));
    CHECK(gpu.display_vram_y() == ((0x012345u >> 10u) & 0x1FFu));

    CHECK(gpu.write_gp1(0x06ABC123u));
    CHECK(gpu.horizontal_start() == 0x123u);
    CHECK(gpu.horizontal_end() == 0xABCu);

    CHECK(gpu.write_gp1(0x07033445u));
    CHECK(gpu.vertical_start() == 0x045u);
    CHECK(gpu.vertical_end() == 0x0CDu);

    CHECK(gpu.write_gp1(0x0800003Fu));
    CHECK(gpu.display_mode() == 0x3Fu);
    CHECK(gpu.gp1_command_count() == 6u);

    const auto hash_before_unsupported = gpu.diagnostic_state_hash();
    CHECK(!gpu.write_gp1(0x09000000u));
    CHECK(gpu.gp1_command_count() == 6u);
    CHECK(gpu.diagnostic_state_hash() == hash_before_unsupported);

    CHECK(gpu.write_gp1(0x00000000u));
    CHECK(gpu.gpu_stat() == 0x14802000u);
    CHECK(gpu.display_disabled());
    CHECK(gpu.dma_direction() == 0u);
    CHECK(gpu.display_vram_x() == 0u);
    CHECK(gpu.display_vram_y() == 0u);
    CHECK(gpu.horizontal_start() == 0x200u);
    CHECK(gpu.horizontal_end() == 0xC00u);
    CHECK(gpu.vertical_start() == 0x010u);
    CHECK(gpu.vertical_end() == 0x100u);
    CHECK(gpu.display_mode() == 0u);
}

static void test_gp1_mmio_bypasses_diagnostic_shadow_and_rejects_unknown_command() {
    jojo::Ps1MemoryBus bus;
    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();

    CHECK(bus.write32(0x1F801814u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    const auto stat = bus.read32(0x1F801814u);
    CHECK(stat.status == jojo::R3000aBusStatus::ok);
    CHECK(stat.value == 0x14802000u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    bus.clear_last_unsupported_access();
    bus.clear_last_diagnostic_mmio_probe();
    const auto unsupported = bus.write32(0x1F801814u, 0x09000000u);
    CHECK(unsupported.status == jojo::R3000aBusStatus::unsupported);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.last_unsupported_access().has_value());
    if (bus.last_unsupported_access()) {
        CHECK(bus.last_unsupported_access()->guest_address == 0x1F801814u);
        CHECK(bus.last_unsupported_access()->physical_address == 0x1F801814u);
        CHECK(bus.last_unsupported_access()->width == 4u);
        CHECK(bus.last_unsupported_access()->write);
        CHECK(bus.last_unsupported_access()->value == 0x09000000u);
    }
}

static void test_gp1_gpu_version_query_latches_gp0_read_value() {
    jojo::Ps1MemoryBus bus;
    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();

    CHECK(bus.write32(0x1F801814u, 0x10000007u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    const auto version = bus.read32(0x1F801810u);
    CHECK(version.status == jojo::R3000aBusStatus::ok);
    CHECK(version.value == 2u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    bus.clear_last_unsupported_access();
    CHECK(bus.write32(0x1F801814u, 0x10000006u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.last_unsupported_access().has_value());
}

static void test_gp0_port_accepts_known_nop_and_rejects_unknown_without_probe_shadow() {
    jojo::Ps1MemoryBus bus;
    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();
    const auto before = bus.diagnostic_state_hash();

    CHECK(bus.write32(0x1F801810u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.diagnostic_state_hash() != before);

    bus.clear_last_unsupported_access();
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F801810u, 0xFF000000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.last_unsupported_access().has_value());
}

static void test_dma2_registers_are_real_mmio_but_started_linked_list_stays_strict() {
    jojo::Ps1MemoryBus bus;
    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();

    CHECK(bus.write32(0x1F8010A0u, 0x80123456u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write32(0x1F8010A4u, 0x00100020u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write32(0x1F8010A8u, 0x00000401u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    const auto madr = bus.read32(0x1F8010A0u);
    const auto bcr = bus.read32(0x1F8010A4u);
    const auto chcr = bus.read32(0x1F8010A8u);
    CHECK(madr.status == jojo::R3000aBusStatus::ok);
    CHECK(madr.value == 0x00123456u);
    CHECK(bcr.status == jojo::R3000aBusStatus::ok);
    CHECK(bcr.value == 0x00100020u);
    CHECK(chcr.status == jojo::R3000aBusStatus::ok);
    CHECK(chcr.value == 0x00000401u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    bus.clear_last_unsupported_access();
    CHECK(bus.write32(0x1F8010A8u, 0x01000401u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.last_unsupported_access().has_value());
    CHECK(bus.read32(0x1F8010A8u).value == 0x00000401u);
}

static void test_b0_write_handles_dummy_stdout_without_host_side_effects() {
    jojo::Ps1MemoryBus bus;
    CHECK(bus.write8(0x00001000u, 'J').status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x00001001u, 'o').status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x00001002u, '!').status == jojo::R3000aBusStatus::ok);

    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000B0u;
    cpu.next_pc = 0x000000B4u;
    cpu.gpr[31] = 0x80012340u;
    const auto hash_before = bios.diagnostic_state_hash();
    const jojo::Ps1HleBiosCall write_stdout{
        jojo::Ps1HleBiosDomain::b0, 0x35u, cpu.pc,
        1u, 0x00001000u, 3u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(write_stdout, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 3u);
    CHECK(cpu.pc == 0x80012340u);
    CHECK(bios.diagnostic_state_hash() == hash_before);

    jojo::R3000aState other_cpu{};
    other_cpu.pc = 0xB0u;
    other_cpu.next_pc = 0xB4u;
    other_cpu.gpr[31] = 0x80012400u;
    const jojo::Ps1HleBiosCall write_other{
        jojo::Ps1HleBiosDomain::b0, 0x35u, other_cpu.pc,
        2u, 0x00001000u, 3u, 0u, other_cpu.gpr[31]};
    CHECK(bios.dispatch(write_other, other_cpu, bus).disposition == jojo::Ps1HleBiosDisposition::unsupported);
}

static void test_a0_gpu_cw_syncs_immediately_and_routes_word_to_gp0() {
    jojo::Ps1MemoryBus bus;
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000A0u;
    cpu.next_pc = 0x000000A4u;
    cpu.gpr[2] = 0xDEADBEEFu;
    cpu.gpr[31] = 0x80012500u;

    const jojo::Ps1HleBiosCall gpu_cw{
        jojo::Ps1HleBiosDomain::a0, 0x49u, cpu.pc,
        0x00000000u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(gpu_cw, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0u);
    CHECK(cpu.pc == 0x80012500u);
}

int main() {
    test_gp1_reset_state_and_supported_control_commands();
    test_gp1_mmio_bypasses_diagnostic_shadow_and_rejects_unknown_command();
    test_gp1_gpu_version_query_latches_gp0_read_value();
    test_gp0_port_accepts_known_nop_and_rejects_unknown_without_probe_shadow();
    test_dma2_registers_are_real_mmio_but_started_linked_list_stays_strict();
    test_b0_write_handles_dummy_stdout_without_host_side_effects();
    test_a0_gpu_cw_syncs_immediately_and_routes_word_to_gp0();
    return failures ? 1 : 0;
}
