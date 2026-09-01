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

constexpr std::uint16_t clut_attr(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::uint16_t>(((x / 16u) & 0x3fu) | ((y & 0x1ffu) << 6u));
}

constexpr std::uint16_t texpage_attr(std::uint32_t page_x_words,
                                     std::uint32_t depth,
                                     std::uint32_t abr = 0u) {
    return static_cast<std::uint16_t>(
        ((page_x_words / 64u) & 0x0fu) |
        ((abr & 3u) << 5u) |
        ((depth & 3u) << 7u));
}

constexpr std::uint32_t uv_word(std::uint32_t u,
                                std::uint32_t v,
                                std::uint16_t attribute = 0u) {
    return (u & 0xffu) | ((v & 0xffu) << 8u) |
           (static_cast<std::uint32_t>(attribute) << 16u);
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

void test_gouraud_triangle_interpolates_vertex_colors() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x300000ffu); // gouraud triangle: vertex 1 red
    gp0(bus, vertex(40, 40));
    gp0(bus, 0x0000ff00u); // vertex 2 green
    gp0(bus, vertex(48, 40));
    gp0(bus, 0x00ff0000u); // vertex 3 blue
    gp0(bus, vertex(40, 48));

    POLY_REQUIRE(bus.gpu_vram[40u * jojo::PsxBus::gpu_vram_width + 40u] == 0x043bu);
    POLY_REQUIRE(bus.gpu_vram[42u * jojo::PsxBus::gpu_vram_width + 42u] == 0x252bu);
    POLY_REQUIRE(bus.gpu_vram[40u * jojo::PsxBus::gpu_vram_width + 46u] == 0x0723u);
    POLY_REQUIRE(bus.gpu_vram[46u * jojo::PsxBus::gpu_vram_width + 40u] == 0x6423u);
}

void test_gouraud_quad_interpolates_second_triangle() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    gp0(bus, 0x380000ffu); // v1 red
    gp0(bus, vertex(60, 60));
    gp0(bus, 0x0000ff00u); // v2 green
    gp0(bus, vertex(68, 60));
    gp0(bus, 0x00ff0000u); // v3 blue
    gp0(bus, vertex(60, 68));
    gp0(bus, 0x00ffffffu); // v4 white
    gp0(bus, vertex(68, 68));

    POLY_REQUIRE(bus.gpu_vram[66u * jojo::PsxBus::gpu_vram_width + 66u] == 0x6733u);
    POLY_REQUIRE(bus.gpu_vram[68u * jojo::PsxBus::gpu_vram_width + 68u] == 0u);
}

void test_raw_flat_textured_triangle_uses_packet_clut_and_texpage() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 64u;
    constexpr std::uint32_t palette_x = 16u;
    constexpr std::uint32_t palette_y = 200u;
    constexpr std::uint32_t u = 4u;
    constexpr std::uint32_t v = 5u;

    gp0(bus, 0xe1000100u); // deliberately wrong page/depth: packet Texpage must win.
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u / 4u] = 0x0003u;
    bus.gpu_vram[palette_y * jojo::PsxBus::gpu_vram_width + palette_x + 3u] = 0x3456u;

    gp0(bus, 0x25000000u); // flat opaque textured raw triangle
    gp0(bus, vertex(80, 80));
    gp0(bus, uv_word(u, v, clut_attr(palette_x, palette_y)));
    gp0(bus, vertex(88, 80));
    gp0(bus, uv_word(u, v, texpage_attr(page_x, 0u)));
    gp0(bus, vertex(80, 88));
    gp0(bus, uv_word(u, v));

    POLY_REQUIRE(bus.gpu_vram[80u * jojo::PsxBus::gpu_vram_width + 80u] == 0x3456u);
}

void test_raw_flat_textured_triangle_interpolates_uv() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 128u;
    gp0(bus, 0xe1000000u); // wrong page/depth on purpose.
    bus.gpu_vram[2u * jojo::PsxBus::gpu_vram_width + page_x + 2u] = 0x2abcu;

    gp0(bus, 0x25000000u);
    gp0(bus, vertex(100, 100));
    gp0(bus, uv_word(0u, 0u));
    gp0(bus, vertex(108, 100));
    gp0(bus, uv_word(8u, 0u, texpage_attr(page_x, 2u)));
    gp0(bus, vertex(100, 108));
    gp0(bus, uv_word(0u, 8u));

    // Pixel centre (102.5,102.5) interpolates to UV(2,2) with truncation.
    POLY_REQUIRE(bus.gpu_vram[102u * jojo::PsxBus::gpu_vram_width + 102u] == 0x2abcu);
}

void test_raw_gouraud_textured_triangle_consumes_colors() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 192u;
    constexpr std::uint32_t u = 3u;
    constexpr std::uint32_t v = 4u;
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = 0x1555u;

    gp0(bus, 0x350000ffu); // gouraud + textured + raw; colors are consumed but ignored.
    gp0(bus, vertex(120, 120));
    gp0(bus, uv_word(u, v));
    gp0(bus, 0x0000ff00u);
    gp0(bus, vertex(128, 120));
    gp0(bus, uv_word(u, v, texpage_attr(page_x, 2u)));
    gp0(bus, 0x00ff0000u);
    gp0(bus, vertex(120, 128));
    gp0(bus, uv_word(u, v));

    POLY_REQUIRE(bus.gpu_vram[120u * jojo::PsxBus::gpu_vram_width + 120u] == 0x1555u);
}

void test_raw_gouraud_textured_quad_uses_twelve_word_packet() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 256u;
    constexpr std::uint32_t u = 2u;
    constexpr std::uint32_t v = 2u;
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = 0x2666u;

    gp0(bus, 0x3d0000ffu);
    gp0(bus, vertex(140, 140));
    gp0(bus, uv_word(u, v));
    gp0(bus, 0x0000ff00u);
    gp0(bus, vertex(148, 140));
    gp0(bus, uv_word(u, v, texpage_attr(page_x, 2u)));
    gp0(bus, 0x00ff0000u);
    gp0(bus, vertex(140, 148));
    gp0(bus, uv_word(u, v));
    gp0(bus, 0x00ffffffu);
    gp0(bus, vertex(148, 148));
    gp0(bus, uv_word(u, v));

    POLY_REQUIRE(bus.gpu_vram[146u * jojo::PsxBus::gpu_vram_width + 146u] == 0x2666u);
}

void test_flat_textured_modulation_and_stp_blending() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 320u;
    constexpr std::uint32_t u = 1u;
    constexpr std::uint32_t v = 1u;
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = 0x4210u;

    gp0(bus, 0x24808080u); // neutral modulation
    gp0(bus, vertex(160, 160));
    gp0(bus, uv_word(u, v));
    gp0(bus, vertex(168, 160));
    gp0(bus, uv_word(u, v, texpage_attr(page_x, 2u)));
    gp0(bus, vertex(160, 168));
    gp0(bus, uv_word(u, v));
    POLY_REQUIRE(bus.gpu_vram[160u * jojo::PsxBus::gpu_vram_width + 160u] == 0x4210u);

    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = 0x801fu;
    bus.gpu_vram[180u * jojo::PsxBus::gpu_vram_width + 180u] = 0x03e0u;
    gp0(bus, 0x26808080u); // semi-transparent modulated triangle
    gp0(bus, vertex(180, 180));
    gp0(bus, uv_word(u, v));
    gp0(bus, vertex(188, 180));
    gp0(bus, uv_word(u, v, texpage_attr(page_x, 2u, 1u))); // ABR=1 B+F
    gp0(bus, vertex(180, 188));
    gp0(bus, uv_word(u, v));
    POLY_REQUIRE(bus.gpu_vram[180u * jojo::PsxBus::gpu_vram_width + 180u] == 0x83ffu);
}

struct PolygonContractRunner {
    PolygonContractRunner() {
        test_flat_untextured_triangle_rasterizes_interior();
        test_flat_untextured_quad_uses_documented_triangle_split();
        test_gouraud_triangle_interpolates_vertex_colors();
        test_gouraud_quad_interpolates_second_triangle();
        test_raw_flat_textured_triangle_uses_packet_clut_and_texpage();
        test_raw_flat_textured_triangle_interpolates_uv();
        test_raw_gouraud_textured_triangle_consumes_colors();
        test_raw_gouraud_textured_quad_uses_twelve_word_packet();
        test_flat_textured_modulation_and_stp_blending();
    }
};

PolygonContractRunner polygon_contract_runner{};

} // namespace
