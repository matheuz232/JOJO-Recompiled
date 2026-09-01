#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void status_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPUSTAT contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define STATUS_REQUIRE(expr) do { if (!(expr)) status_fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    STATUS_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
                   jojo::PsxBusAccessReason::ok);
}

void gp1(jojo::PsxBus& bus, std::uint32_t value) {
    STATUS_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp1_address, value) ==
                   jojo::PsxBusAccessReason::ok);
}

std::uint32_t status(jojo::PsxBus& bus) {
    const auto result = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address);
    STATUS_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

std::uint32_t gpuread(jojo::PsxBus& bus) {
    const auto result = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp0_address);
    STATUS_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

constexpr std::uint32_t coord(std::uint32_t x, std::uint32_t y) {
    return (x & 0xffffu) | ((y & 0xffffu) << 16u);
}

void test_ready_command_word_tracks_packet_boundaries() {
    jojo::PsxBus bus{};
    STATUS_REQUIRE((status(bus) & (1u << 26u)) != 0u);

    gp0(bus, 0x600000ffu); // variable-size untextured rectangle, needs xy + wh
    STATUS_REQUIRE((status(bus) & (1u << 26u)) == 0u);
    gp0(bus, coord(10u, 10u));
    STATUS_REQUIRE((status(bus) & (1u << 26u)) == 0u);
    gp0(bus, coord(2u, 2u));
    STATUS_REQUIRE((status(bus) & (1u << 26u)) != 0u);
}

void test_ready_dma_block_drops_during_polygon_parameters() {
    jojo::PsxBus bus{};
    gp1(bus, 0x04000002u); // CPU -> GP0, GPUSTAT.25 mirrors bit28
    auto idle = status(bus);
    STATUS_REQUIRE((idle & (1u << 28u)) != 0u);
    STATUS_REQUIRE((idle & (1u << 25u)) != 0u);

    gp0(bus, 0x200000ffu); // flat triangle command word
    auto receiving = status(bus);
    STATUS_REQUIRE((receiving & (1u << 28u)) == 0u);
    STATUS_REQUIRE((receiving & (1u << 25u)) == 0u);

    gp0(bus, coord(10u, 10u));
    gp0(bus, coord(20u, 10u));
    gp0(bus, coord(10u, 20u));
    auto complete = status(bus);
    STATUS_REQUIRE((complete & (1u << 28u)) != 0u);
    STATUS_REQUIRE((complete & (1u << 25u)) != 0u);
}

void test_dma_request_meaning_follows_gp1_direction() {
    jojo::PsxBus bus{};

    gp1(bus, 0x04000000u);
    STATUS_REQUIRE((status(bus) & (1u << 25u)) == 0u);

    gp1(bus, 0x04000001u); // FIFO mode; this model never fills the 16-word FIFO
    STATUS_REQUIRE((status(bus) & (1u << 25u)) != 0u);

    gp1(bus, 0x04000003u); // GPUREAD -> CPU, GPUSTAT.25 mirrors bit27
    STATUS_REQUIRE((status(bus) & (1u << 25u)) == 0u);

    bus.gpu_vram[5u * jojo::PsxBus::gpu_vram_width + 7u] = 0x1234u;
    bus.gpu_vram[5u * jojo::PsxBus::gpu_vram_width + 8u] = 0x5678u;
    gp0(bus, 0xc0000000u);
    gp0(bus, coord(7u, 5u));
    gp0(bus, coord(2u, 1u));
    const auto ready = status(bus);
    STATUS_REQUIRE((ready & (1u << 27u)) != 0u);
    STATUS_REQUIRE((ready & (1u << 25u)) != 0u);

    STATUS_REQUIRE(gpuread(bus) == 0x56781234u);
    const auto done = status(bus);
    STATUS_REQUIRE((done & (1u << 27u)) == 0u);
    STATUS_REQUIRE((done & (1u << 25u)) == 0u);
}

void test_gpu_irq_request_latches_istat_only_on_source_rising_edge() {
    jojo::PsxBus bus{};
    constexpr std::uint16_t gpu_irq = 1u << 1u;

    STATUS_REQUIRE((status(bus) & (1u << 24u)) == 0u);
    STATUS_REQUIRE((bus.interrupt_status & gpu_irq) == 0u);

    gp0(bus, 0x1f000000u);
    STATUS_REQUIRE((status(bus) & (1u << 24u)) != 0u);
    STATUS_REQUIRE((bus.interrupt_status & gpu_irq) != 0u);

    // I_STAT is latched independently from the GPU source. Clearing I_STAT
    // while GPUSTAT.24 remains high must not synthesize a second edge.
    STATUS_REQUIRE(jojo::psx_bus_write_u16(
                       bus, jojo::PsxBus::interrupt_status_address,
                       static_cast<std::uint16_t>(jojo::PsxBus::interrupt_status_valid_bits & ~gpu_irq)) ==
                   jojo::PsxBusAccessReason::ok);
    STATUS_REQUIRE((bus.interrupt_status & gpu_irq) == 0u);
    STATUS_REQUIRE((status(bus) & (1u << 24u)) != 0u);

    gp0(bus, 0x1f000000u);
    STATUS_REQUIRE((bus.interrupt_status & gpu_irq) == 0u);

    // GP1(02h) lowers the GPU IRQ source. The next GP0(1Fh) therefore creates
    // a fresh rising edge and relatches I_STAT.1.
    gp1(bus, 0x02000000u);
    STATUS_REQUIRE((status(bus) & (1u << 24u)) == 0u);
    STATUS_REQUIRE((bus.interrupt_status & gpu_irq) == 0u);

    gp0(bus, 0x1f000000u);
    STATUS_REQUIRE((status(bus) & (1u << 24u)) != 0u);
    STATUS_REQUIRE((bus.interrupt_status & gpu_irq) != 0u);
}

struct StatusContractRunner {
    StatusContractRunner() {
        test_ready_command_word_tracks_packet_boundaries();
        test_ready_dma_block_drops_during_polygon_parameters();
        test_dma_request_meaning_follows_gp1_direction();
        test_gpu_irq_request_latches_istat_only_on_source_rising_edge();
    }
};

StatusContractRunner status_contract_runner{};

} // namespace
