#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void dma2_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d DMA2 continuous contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define DMA2_REQUIRE(expr) do { if (!(expr)) dma2_fail(#expr, __LINE__); } while (0)

void write_ram_word(jojo::PsxBus& bus, std::uint32_t address, std::uint32_t value) {
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, address, value) ==
                 jojo::PsxBusAccessReason::ok);
}

std::uint32_t read_ram_word(jojo::PsxBus& bus, std::uint32_t address) {
    const auto read = jojo::psx_bus_read_u32(bus, address);
    DMA2_REQUIRE(read.reason == jojo::PsxBusAccessReason::ok);
    return read.value;
}

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
                 jojo::PsxBusAccessReason::ok);
}

void gp1(jojo::PsxBus& bus, std::uint32_t value) {
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp1_address, value) ==
                 jojo::PsxBusAccessReason::ok);
}

constexpr std::uint32_t coord(std::uint32_t x, std::uint32_t y) {
    return (x & 0xffffu) | ((y & 0xffffu) << 16u);
}

void enable_dma2_irq(jojo::PsxBus& bus) {
    DMA2_REQUIRE(jojo::psx_bus_write_u32(
                     bus, jojo::PsxBus::dma_interrupt_address,
                     (1u << 23u) | (1u << 18u)) == jojo::PsxBusAccessReason::ok);
}

void test_sync1_ram_to_gp0_uploads_vram_and_completes() {
    jojo::PsxBus bus{};
    constexpr std::uint32_t source = 0x00000100u;

    write_ram_word(bus, source + 0u, 0xa0000000u);
    write_ram_word(bus, source + 4u, coord(30u, 40u));
    write_ram_word(bus, source + 8u, coord(2u, 1u));
    write_ram_word(bus, source + 12u, 0x22221111u);

    gp1(bus, 0x04000002u); // DMA2 RAM -> GP0
    enable_dma2_irq(bus);
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_base_address,
                                         source) == jojo::PsxBusAccessReason::ok);
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_block_control_address,
                                         0x00010004u) == jojo::PsxBusAccessReason::ok);

    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_channel_control_address,
                                         0x01000201u) == jojo::PsxBusAccessReason::ok);

    DMA2_REQUIRE(bus.gpu_vram[40u * jojo::PsxBus::gpu_vram_width + 30u] == 0x1111u);
    DMA2_REQUIRE(bus.gpu_vram[40u * jojo::PsxBus::gpu_vram_width + 31u] == 0x2222u);
    DMA2_REQUIRE(bus.dma2_base == source + 16u);
    DMA2_REQUIRE(bus.dma2_block_control == 0x00000004u);
    DMA2_REQUIRE((bus.dma2_channel_control & jojo::PsxBus::dma_channel_start_busy) == 0u);
    DMA2_REQUIRE((bus.dma_interrupt & (1u << 26u)) != 0u);
    DMA2_REQUIRE((bus.interrupt_status & (1u << 3u)) != 0u);
}

void test_sync1_gpuread_to_ram_consumes_vram_stream() {
    jojo::PsxBus bus{};
    constexpr std::uint32_t destination = 0x00000200u;
    constexpr std::uint32_t x = 100u;
    constexpr std::uint32_t y = 70u;
    bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + x + 0u] = 0x1111u;
    bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + x + 1u] = 0x2222u;
    bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + x + 2u] = 0x3333u;
    bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + x + 3u] = 0x4444u;

    gp0(bus, 0xc0000000u);
    gp0(bus, coord(x, y));
    gp0(bus, coord(4u, 1u));
    DMA2_REQUIRE((jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address).value &
                  (1u << 27u)) != 0u);

    gp1(bus, 0x04000003u); // DMA2 GPUREAD -> RAM
    enable_dma2_irq(bus);
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_base_address,
                                         destination) == jojo::PsxBusAccessReason::ok);
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_block_control_address,
                                         0x00010002u) == jojo::PsxBusAccessReason::ok);

    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_channel_control_address,
                                         0x01000200u) == jojo::PsxBusAccessReason::ok);

    DMA2_REQUIRE(read_ram_word(bus, destination + 0u) == 0x22221111u);
    DMA2_REQUIRE(read_ram_word(bus, destination + 4u) == 0x44443333u);
    DMA2_REQUIRE(bus.dma2_base == destination + 8u);
    DMA2_REQUIRE(bus.dma2_block_control == 0x00000002u);
    DMA2_REQUIRE((bus.dma2_channel_control & jojo::PsxBus::dma_channel_start_busy) == 0u);
    DMA2_REQUIRE((jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address).value &
                  (1u << 27u)) == 0u);
    DMA2_REQUIRE((bus.dma_interrupt & (1u << 26u)) != 0u);
}

void test_sync1_gpuread_to_ram_requires_gpu_dma_direction() {
    jojo::PsxBus bus{};
    bus.gpu_vram[5u * jojo::PsxBus::gpu_vram_width + 5u] = 0x1234u;
    bus.gpu_vram[5u * jojo::PsxBus::gpu_vram_width + 6u] = 0x5678u;
    gp0(bus, 0xc0000000u);
    gp0(bus, coord(5u, 5u));
    gp0(bus, coord(2u, 1u));

    gp1(bus, 0x04000002u); // wrong: RAM -> GP0
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_base_address,
                                         0x300u) == jojo::PsxBusAccessReason::ok);
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_block_control_address,
                                         0x00010001u) == jojo::PsxBusAccessReason::ok);
    DMA2_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::dma2_channel_control_address,
                                         0x01000200u) == jojo::PsxBusAccessReason::unmapped);
    DMA2_REQUIRE((jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address).value &
                  (1u << 27u)) != 0u);
    DMA2_REQUIRE(read_ram_word(bus, 0x300u) == 0u);
}

struct Dma2ContinuousContractRunner {
    Dma2ContinuousContractRunner() {
        test_sync1_ram_to_gp0_uploads_vram_and_completes();
        test_sync1_gpuread_to_ram_consumes_vram_stream();
        test_sync1_gpuread_to_ram_requires_gpu_dma_direction();
    }
};

Dma2ContinuousContractRunner dma2_continuous_contract_runner{};

} // namespace
