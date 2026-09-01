#include "core/psx_bus.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d DMA3 contract failed: %s\n", __FILE__, line, expression);
    std::exit(1);
}

#define REQUIRE(expr) do { if (!(expr)) fail(#expr, __LINE__); } while (0)

void run_dma3_cdrom_contract() {
    constexpr std::uint32_t dma3_madr = 0x1f8010b0u;
    constexpr std::uint32_t dma3_bcr = 0x1f8010b4u;
    constexpr std::uint32_t dma3_chcr = 0x1f8010b8u;
    constexpr std::uint32_t destination = 0x00010000u;
    constexpr std::uint32_t words_per_sector = 2048u / 4u;
    constexpr std::uint32_t dma3_burst_device_to_ram = 0x11000000u;

    jojo::PsxBus bus{};
    for (std::size_t i = 0; i < bus.cdrom_sector_buffer.size(); ++i) {
        bus.cdrom_sector_buffer[i] = static_cast<std::uint8_t>((i * 37u + 11u) & 0xffu);
    }
    bus.cdrom_sector_buffer_ready = true;

    REQUIRE(jojo::psx_bus_write_u8(bus, jojo::PsxBus::cdrom_base_address, 0u) ==
            jojo::PsxBusAccessReason::ok);
    REQUIRE(jojo::psx_bus_write_u8(bus, jojo::PsxBus::cdrom_base_address + 3u, 0x80u) ==
            jojo::PsxBusAccessReason::ok);
    REQUIRE(bus.cdrom_data_count == jojo::PsxBus::cdrom_data_sector_size);

    // DICR master enable plus channel-3 interrupt enable. Completion must set
    // channel-3 flag (bit 27) and raise DMA in I_STAT (bit 3).
    REQUIRE(jojo::psx_bus_write_u32(
                bus, jojo::PsxBus::dma_interrupt_address,
                (1u << 23u) | (1u << 19u)) == jojo::PsxBusAccessReason::ok);

    REQUIRE(jojo::psx_bus_write_u32(bus, dma3_madr, destination) ==
            jojo::PsxBusAccessReason::ok);
    REQUIRE(jojo::psx_bus_write_u32(bus, dma3_bcr, words_per_sector) ==
            jojo::PsxBusAccessReason::ok);
    REQUIRE(jojo::psx_bus_read_u32(bus, dma3_madr).value == destination);
    REQUIRE(jojo::psx_bus_read_u32(bus, dma3_bcr).value == words_per_sector);

    REQUIRE(jojo::psx_bus_write_u32(bus, dma3_chcr, dma3_burst_device_to_ram) ==
            jojo::PsxBusAccessReason::ok);

    for (std::size_t i = 0; i < jojo::PsxBus::cdrom_data_sector_size; ++i) {
        const auto expected = static_cast<std::uint8_t>((i * 37u + 11u) & 0xffu);
        REQUIRE(bus.ram[destination + i] == expected);
    }
    REQUIRE(bus.cdrom_data_count == 0u);

    const auto chcr = jojo::psx_bus_read_u32(bus, dma3_chcr);
    REQUIRE(chcr.reason == jojo::PsxBusAccessReason::ok);
    REQUIRE((chcr.value & jojo::PsxBus::dma_channel_start_busy) == 0u);

    const auto dicr = jojo::psx_bus_read_u32(bus, jojo::PsxBus::dma_interrupt_address);
    REQUIRE(dicr.reason == jojo::PsxBusAccessReason::ok);
    REQUIRE((dicr.value & (1u << 27u)) != 0u);
    REQUIRE((dicr.value & jojo::PsxBus::dma_interrupt_master_flag) != 0u);
    REQUIRE((bus.interrupt_status & (1u << 3u)) != 0u);
}

struct Dma3ContractRunner {
    Dma3ContractRunner() { run_dma3_cdrom_contract(); }
};

Dma3ContractRunner dma3_contract_runner{};

} // namespace
