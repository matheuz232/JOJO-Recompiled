#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void line_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPU line contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define LINE_REQUIRE(expr) do { if (!(expr)) line_fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    LINE_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
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

void test_flat_line_includes_both_endpoints() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x400000ffu);
    gp0(bus, vertex(10, 10));
    gp0(bus, vertex(14, 10));

    for (std::uint32_t x = 10u; x <= 14u; ++x) {
        LINE_REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + x] == 0x001fu);
    }
    LINE_REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 15u] == 0u);
}

void test_overlapping_line_draws_one_pixel() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x4000ff00u);
    gp0(bus, vertex(16, 16));
    gp0(bus, vertex(16, 16));

    LINE_REQUIRE(bus.gpu_vram[16u * jojo::PsxBus::gpu_vram_width + 16u] == 0x03e0u);
}

void test_gouraud_line_interpolates_color() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x500000ffu); // red
    gp0(bus, vertex(20, 20));
    gp0(bus, 0x00ff0000u); // blue
    gp0(bus, vertex(24, 20));

    LINE_REQUIRE(bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 20u] == 0x001fu);
    LINE_REQUIRE(bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 22u] == 0x3c0fu);
    LINE_REQUIRE(bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 24u] == 0x7c00u);
}

void test_semitransparent_line_uses_abr() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);
    gp0(bus, 0xe1000020u); // ABR=1, B+F
    bus.gpu_vram[30u * jojo::PsxBus::gpu_vram_width + 30u] = 0x03e0u;

    gp0(bus, 0x420000ffu);
    gp0(bus, vertex(30, 30));
    gp0(bus, vertex(30, 30));

    LINE_REQUIRE(bus.gpu_vram[30u * jojo::PsxBus::gpu_vram_width + 30u] == 0x03ffu);
}

void test_flat_polyline_streams_segments_and_terminates() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x480000ffu);
    gp0(bus, vertex(40, 40));
    gp0(bus, vertex(44, 40));
    gp0(bus, vertex(44, 44));
    gp0(bus, 0x50005000u);

    LINE_REQUIRE(bus.gpu_vram[40u * jojo::PsxBus::gpu_vram_width + 42u] == 0x001fu);
    LINE_REQUIRE(bus.gpu_vram[42u * jojo::PsxBus::gpu_vram_width + 44u] == 0x001fu);
    LINE_REQUIRE(bus.gpu_vram[44u * jojo::PsxBus::gpu_vram_width + 44u] == 0x001fu);
    LINE_REQUIRE(bus.gpu_gp0_packet_count == 0u);
    LINE_REQUIRE(bus.gpu_gp0_packet_words == 0u);
}

void test_gouraud_polyline_terminator_is_color_word() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x580000ffu); // red
    gp0(bus, vertex(50, 50));
    gp0(bus, 0x0000ff00u); // green
    gp0(bus, vertex(54, 50));
    gp0(bus, 0x00ff0000u); // blue
    gp0(bus, vertex(54, 54));
    gp0(bus, 0x50005000u);

    LINE_REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 50u] == 0x001fu);
    LINE_REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 54u] == 0x03e0u);
    LINE_REQUIRE(bus.gpu_vram[54u * jojo::PsxBus::gpu_vram_width + 54u] == 0x7c00u);
    LINE_REQUIRE(bus.gpu_gp0_packet_count == 0u);
    LINE_REQUIRE(bus.gpu_gp0_packet_words == 0u);
}

struct LineContractRunner {
    LineContractRunner() {
        test_flat_line_includes_both_endpoints();
        test_overlapping_line_draws_one_pixel();
        test_gouraud_line_interpolates_color();
        test_semitransparent_line_uses_abr();
        test_flat_polyline_streams_segments_and_terminates();
        test_gouraud_polyline_terminator_is_color_word();
    }
};

LineContractRunner line_contract_runner{};

} // namespace
