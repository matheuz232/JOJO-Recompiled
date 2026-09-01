#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void transfer_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPU transfer contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define TRANSFER_REQUIRE(expr) do { if (!(expr)) transfer_fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    TRANSFER_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
                     jojo::PsxBusAccessReason::ok);
}

void gp1(jojo::PsxBus& bus, std::uint32_t value) {
    TRANSFER_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp1_address, value) ==
                     jojo::PsxBusAccessReason::ok);
}

std::uint32_t status(jojo::PsxBus& bus) {
    const auto result = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address);
    TRANSFER_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

std::uint32_t gpuread(jojo::PsxBus& bus) {
    const auto result = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp0_address);
    TRANSFER_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

constexpr std::uint32_t coord(std::uint32_t x, std::uint32_t y) {
    return (x & 0xffffu) | ((y & 0xffffu) << 16u);
}

void test_vram_to_vram_wraps_source_edges() {
    jojo::PsxBus bus{};

    bus.gpu_vram[511u * jojo::PsxBus::gpu_vram_width + 1023u] = 0x1111u;
    bus.gpu_vram[511u * jojo::PsxBus::gpu_vram_width + 0u] = 0x2222u;
    bus.gpu_vram[0u * jojo::PsxBus::gpu_vram_width + 1023u] = 0x3333u;
    bus.gpu_vram[0u * jojo::PsxBus::gpu_vram_width + 0u] = 0x4444u;

    gp0(bus, 0x80000000u);
    gp0(bus, coord(1023u, 511u));
    gp0(bus, coord(100u, 100u));
    gp0(bus, coord(2u, 2u));

    TRANSFER_REQUIRE(bus.gpu_vram[100u * jojo::PsxBus::gpu_vram_width + 100u] == 0x1111u);
    TRANSFER_REQUIRE(bus.gpu_vram[100u * jojo::PsxBus::gpu_vram_width + 101u] == 0x2222u);
    TRANSFER_REQUIRE(bus.gpu_vram[101u * jojo::PsxBus::gpu_vram_width + 100u] == 0x3333u);
    TRANSFER_REQUIRE(bus.gpu_vram[101u * jojo::PsxBus::gpu_vram_width + 101u] == 0x4444u);
}

void test_vram_to_vram_honors_mask_bits() {
    jojo::PsxBus bus{};
    bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 10u] = 0x0123u;
    bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 11u] = 0x0456u;
    bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 20u] = 0x8001u;

    gp0(bus, 0xe6000003u); // protect old bit15 and force bit15 on written pixels
    gp0(bus, 0x80000000u);
    gp0(bus, coord(10u, 10u));
    gp0(bus, coord(20u, 20u));
    gp0(bus, coord(2u, 1u));

    TRANSFER_REQUIRE(bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 20u] == 0x8001u);
    TRANSFER_REQUIRE(bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 21u] == 0x8456u);
}

void test_vram_to_cpu_streams_gpureg_words_and_ready_bit() {
    jojo::PsxBus bus{};
    bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 1023u] = 0xaaaau;
    bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 0u] = 0xbbbbu;
    bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 1u] = 0xccccu;

    gp0(bus, 0xc0000000u);
    gp0(bus, coord(1023u, 20u));
    gp0(bus, coord(3u, 1u));

    TRANSFER_REQUIRE((status(bus) & (1u << 27u)) != 0u);
    TRANSFER_REQUIRE(gpuread(bus) == 0xbbbbaaaau);
    TRANSFER_REQUIRE((status(bus) & (1u << 27u)) != 0u);
    TRANSFER_REQUIRE((gpuread(bus) & 0xffffu) == 0xccccu);
    TRANSFER_REQUIRE((status(bus) & (1u << 27u)) == 0u);
}

void test_gpu_reset_cancels_vram_to_cpu_stream() {
    jojo::PsxBus bus{};
    bus.gpu_vram[3u * jojo::PsxBus::gpu_vram_width + 4u] = 0x1234u;

    gp0(bus, 0xc0000000u);
    gp0(bus, coord(4u, 3u));
    gp0(bus, coord(1u, 1u));
    TRANSFER_REQUIRE((status(bus) & (1u << 27u)) != 0u);

    gp1(bus, 0x00000000u);
    TRANSFER_REQUIRE((status(bus) & (1u << 27u)) == 0u);
    TRANSFER_REQUIRE(gpuread(bus) == 0u);
}

struct TransferContractRunner {
    TransferContractRunner() {
        test_vram_to_vram_wraps_source_edges();
        test_vram_to_vram_honors_mask_bits();
        test_vram_to_cpu_streams_gpureg_words_and_ready_bit();
        test_gpu_reset_cancels_vram_to_cpu_stream();
    }
};

TransferContractRunner transfer_contract_runner{};

} // namespace
