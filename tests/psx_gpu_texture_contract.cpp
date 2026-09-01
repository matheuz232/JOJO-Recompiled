#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void texture_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPU texture contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define TEX_REQUIRE(expr) do { if (!(expr)) texture_fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    TEX_REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
                jojo::PsxBusAccessReason::ok);
}

constexpr std::uint32_t vertex(std::uint32_t x, std::uint32_t y) {
    return (x & 0x7ffu) | ((y & 0x7ffu) << 16u);
}

constexpr std::uint16_t clut_attr(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::uint16_t>(((x / 16u) & 0x3fu) | ((y & 0x1ffu) << 6u));
}

constexpr std::uint32_t uv_word(std::uint32_t u,
                                std::uint32_t v,
                                std::uint16_t clut) {
    return (u & 0xffu) | ((v & 0xffu) << 8u) |
           (static_cast<std::uint32_t>(clut) << 16u);
}

void configure_full_drawing_area(jojo::PsxBus& bus) {
    gp0(bus, 0xe3000000u);
    gp0(bus, 0xe4000000u | 1023u | (511u << 10u));
    gp0(bus, 0xe5000000u);
    gp0(bus, 0xe6000000u);
}

void test_raw_4bit_clut_sprite() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 64u;
    constexpr std::uint32_t page_y = 0u;
    constexpr std::uint32_t u = 4u;
    constexpr std::uint32_t v = 3u;
    constexpr std::uint32_t palette_x = 16u;
    constexpr std::uint32_t palette_y = 100u;

    gp0(bus, 0xe1000001u); // page X=64, 4-bit CLUT mode.
    bus.gpu_vram[(page_y + v) * jojo::PsxBus::gpu_vram_width + page_x + u / 4u] = 0x0002u;
    bus.gpu_vram[palette_y * jojo::PsxBus::gpu_vram_width + palette_x + 2u] = 0x1234u;

    gp0(bus, 0x6d000000u); // opaque, raw textured 1x1 rectangle.
    gp0(bus, vertex(20u, 30u));
    gp0(bus, uv_word(u, v, clut_attr(palette_x, palette_y)));
    TEX_REQUIRE(bus.gpu_vram[30u * jojo::PsxBus::gpu_vram_width + 20u] == 0x1234u);
}

void test_raw_8bit_clut_sprite() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 128u;
    constexpr std::uint32_t u = 3u;
    constexpr std::uint32_t v = 7u;
    constexpr std::uint32_t palette_x = 32u;
    constexpr std::uint32_t palette_y = 110u;

    gp0(bus, 0xe1000082u); // page X=128, 8-bit CLUT mode.
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u / 2u] = 0x0504u;
    bus.gpu_vram[palette_y * jojo::PsxBus::gpu_vram_width + palette_x + 5u] = 0x4567u;

    gp0(bus, 0x6d000000u);
    gp0(bus, vertex(40u, 50u));
    gp0(bus, uv_word(u, v, clut_attr(palette_x, palette_y)));
    TEX_REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 40u] == 0x4567u);
}

void test_raw_15bit_sprite_and_zero_transparency() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 192u;
    constexpr std::uint32_t u = 6u;
    constexpr std::uint32_t v = 9u;
    gp0(bus, 0xe1000103u); // page X=192, direct 15-bit mode.

    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = 0x2abcu;
    gp0(bus, 0x6d000000u);
    gp0(bus, vertex(60u, 70u));
    gp0(bus, uv_word(u, v, 0u));
    TEX_REQUIRE(bus.gpu_vram[70u * jojo::PsxBus::gpu_vram_width + 60u] == 0x2abcu);

    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u + 1u] = 0u;
    bus.gpu_vram[70u * jojo::PsxBus::gpu_vram_width + 61u] = 0x7777u;
    gp0(bus, 0x6d000000u);
    gp0(bus, vertex(61u, 70u));
    gp0(bus, uv_word(u + 1u, v, 0u));
    TEX_REQUIRE(bus.gpu_vram[70u * jojo::PsxBus::gpu_vram_width + 61u] == 0x7777u);
}

void test_variable_texture_window_repeats_source_coordinates() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 256u;
    gp0(bus, 0xe1000104u); // page X=256, direct 15-bit mode.

    // Mask X bit0 and replace it with offset bit0: source U 0..3 becomes 8..11.
    gp0(bus, 0xe2000401u);
    bus.gpu_vram[page_x + 8u] = 0x1111u;
    bus.gpu_vram[page_x + 9u] = 0x2222u;
    bus.gpu_vram[page_x + 10u] = 0x3333u;
    bus.gpu_vram[page_x + 11u] = 0x4444u;

    gp0(bus, 0x65000000u); // opaque, raw textured variable rectangle.
    gp0(bus, vertex(80u, 90u));
    gp0(bus, uv_word(0u, 0u, 0u));
    gp0(bus, (1u << 16u) | 4u);

    TEX_REQUIRE(bus.gpu_vram[90u * jojo::PsxBus::gpu_vram_width + 80u] == 0x1111u);
    TEX_REQUIRE(bus.gpu_vram[90u * jojo::PsxBus::gpu_vram_width + 81u] == 0x2222u);
    TEX_REQUIRE(bus.gpu_vram[90u * jojo::PsxBus::gpu_vram_width + 82u] == 0x3333u);
    TEX_REQUIRE(bus.gpu_vram[90u * jojo::PsxBus::gpu_vram_width + 83u] == 0x4444u);
}

void test_rectangle_texture_xy_flip_decrements_uv() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 320u;
    constexpr std::uint32_t u = 10u;
    constexpr std::uint32_t v = 10u;
    gp0(bus, 0xe1003105u); // page X=320, 15-bit mode, X+Y flip.

    // With both flips, a 2x2 sprite starting at UV(10,10) samples:
    // (10,10), (9,10), (10,9), (9,9).
    bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + page_x + 10u] = 0x1111u;
    bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + page_x + 9u] = 0x2222u;
    bus.gpu_vram[9u * jojo::PsxBus::gpu_vram_width + page_x + 10u] = 0x3333u;
    bus.gpu_vram[9u * jojo::PsxBus::gpu_vram_width + page_x + 9u] = 0x4444u;

    gp0(bus, 0x65000000u);
    gp0(bus, vertex(100u, 120u));
    gp0(bus, uv_word(u, v, 0u));
    gp0(bus, (2u << 16u) | 2u);

    TEX_REQUIRE(bus.gpu_vram[120u * jojo::PsxBus::gpu_vram_width + 100u] == 0x1111u);
    TEX_REQUIRE(bus.gpu_vram[120u * jojo::PsxBus::gpu_vram_width + 101u] == 0x2222u);
    TEX_REQUIRE(bus.gpu_vram[121u * jojo::PsxBus::gpu_vram_width + 100u] == 0x3333u);
    TEX_REQUIRE(bus.gpu_vram[121u * jojo::PsxBus::gpu_vram_width + 101u] == 0x4444u);
}

void test_neutral_texture_modulation_preserves_texel() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 384u;
    constexpr std::uint32_t u = 5u;
    constexpr std::uint32_t v = 6u;
    constexpr std::uint16_t texel = 0x4210u;
    gp0(bus, 0xe1000106u); // page X=384, direct 15-bit mode.
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = texel;

    // Raw bit clear means modulation. RGB 80/80/80 is the hardware-neutral
    // brightness because each channel is texel*128/128.
    gp0(bus, 0x6c808080u);
    gp0(bus, vertex(130u, 140u));
    gp0(bus, uv_word(u, v, 0u));

    TEX_REQUIRE(bus.gpu_vram[140u * jojo::PsxBus::gpu_vram_width + 130u] == texel);
}

void test_texture_modulation_scales_clamps_and_preserves_stp() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 448u;
    constexpr std::uint32_t u = 7u;
    constexpr std::uint32_t v = 8u;
    constexpr std::uint16_t texel = 0xa21fu; // STP=1, B=8, G=16, R=31.
    gp0(bus, 0xe1000107u); // page X=448, direct 15-bit mode.
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = texel;

    // RGB=(255,64,128): red saturates, green halves, blue stays neutral.
    gp0(bus, 0x6c8040ffu);
    gp0(bus, vertex(150u, 160u));
    gp0(bus, uv_word(u, v, 0u));

    TEX_REQUIRE(bus.gpu_vram[160u * jojo::PsxBus::gpu_vram_width + 150u] == 0xa11fu);
}

struct TextureContractRunner {
    TextureContractRunner() {
        test_raw_4bit_clut_sprite();
        test_raw_8bit_clut_sprite();
        test_raw_15bit_sprite_and_zero_transparency();
        test_variable_texture_window_repeats_source_coordinates();
        test_rectangle_texture_xy_flip_decrements_uv();
        test_neutral_texture_modulation_preserves_texel();
        test_texture_modulation_scales_clamps_and_preserves_stp();
    }
};

TextureContractRunner texture_contract_runner{};

} // namespace
