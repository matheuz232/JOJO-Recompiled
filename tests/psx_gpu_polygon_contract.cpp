#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void polygon_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPU polygon contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define POLY_REQUIRE(expr) do { if (!(expr)) polygon_fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    POLY_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
                 jojo::PsxBusAccessReason::ok);
}

constexpr std::uint32_t vertex(std::int32_t x, std::int32_t y) {
    return (static_cast<std::uint32_t>(x) & 0x7ffu) |
           ((static_cast<std::uint32_t>(y) & 0x7ffu) << 16u);
}

void configure_full_drawing_area(jojo::PsxBus& bus) {
    gp0(bus, 0xe3000000u);
    gp0(bus, 0xe4000000u | 1023u | (511u << 10u));
    gp0(bus, 0xe5000000u);
    gp0(bus, 0xe6000000u);
}

void test_flat_untextured_triangle_rasterizes_interior() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x200000ffu); // flat opaque untextured triangle, red
    gp0(bus, vertex(10, 10));
    gp0(bus, vertex(20, 10));
    gp0(bus, vertex(10, 20));

    POLY_REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 10u] == 0x001fu);
    POLY_REQUIRE(bus.gpu_vram[11u * jojo::PsxBus::gpu_vram_width + 11u] == 0x001fu);
    POLY_REQUIRE(bus.gpu_vram[15u * jojo::PsxBus::gpu_vram_width + 12u] == 0x001fu);
    POLY_REQUIRE(bus.gpu_vram[19u * jojo::PsxBus::gpu_vram_width + 19u] == 0u);
    POLY_REQUIRE(bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 10u] == 0u);
}

void test_flat_untextured_quad_uses_documented_triangle_split() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x2800ff00u); // flat opaque untextured quad, green
    gp0(bus, vertex(30, 30));
    gp0(bus, vertex(36, 30));
    gp0(bus, vertex(30, 36));
    gp0(bus, vertex(36, 36));

    POLY_REQUIRE(bus.gpu_vram[30u * jojo::PsxBus::gpu_vram_width + 30u] == 0x03e0u);
    POLY_REQUIRE(bus.gpu_vram[32u * jojo::PsxBus::gpu_vram_width + 32u] == 0x03e0u);
    POLY_REQUIRE(bus.gpu_vram[35u * jojo::PsxBus::gpu_vram_width + 35u] == 0x03e0u);
    POLY_REQUIRE(bus.gpu_vram[36u * jojo::PsxBus::gpu_vram_width + 36u] == 0u);
    POLY_REQUIRE(bus.gpu_vram[35u * jojo::PsxBus::gpu_vram_width + 36u] == 0u);
}

struct PolygonContractRunner {
    PolygonContractRunner() {
        test_flat_untextured_triangle_rasterizes_interior();
        test_flat_untextured_quad_uses_documented_triangle_split();
    }
};

PolygonContractRunner polygon_contract_runner{};

} // namespace
