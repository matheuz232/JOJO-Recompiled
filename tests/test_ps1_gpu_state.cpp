#include "core/ps1_gpu_state.h"
#include "core/ps1_memory_bus.h"

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

int main() {
    test_gp1_reset_state_and_supported_control_commands();
    test_gp1_mmio_bypasses_diagnostic_shadow_and_rejects_unknown_command();
    return failures ? 1 : 0;
}
