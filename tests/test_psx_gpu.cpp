#include "core/psx_bus.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPU contract failed: %s\n", __FILE__, line, expression);
    std::exit(1);
}

#define REQUIRE(expr) do { if (!(expr)) fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
            jojo::PsxBusAccessReason::ok);
}

void gp1(jojo::PsxBus& bus, std::uint32_t value) {
    REQUIRE(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp1_address, value) ==
            jojo::PsxBusAccessReason::ok);
}

std::uint32_t gpu_read(const jojo::PsxBus& bus) {
    const auto read = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp0_address);
    REQUIRE(read.reason == jojo::PsxBusAccessReason::ok);
    return read.value;
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

void test_cpu_to_vram_upload_and_parser_recovery() {
    jojo::PsxBus bus{};

    // GP0(A0h): CPU -> VRAM image load. Pixels are supplied as packed 16-bit
    // VRAM values, two pixels per GP0 word, in row-major order.
    gp0(bus, 0xa0000000u);
    gp0(bus, (50u << 16u) | 100u);
    gp0(bus, (2u << 16u) | 3u);
    gp0(bus, 0x22221111u);
    gp0(bus, 0x44443333u);
    gp0(bus, 0x66665555u);

    REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 100u] == 0x1111u);
    REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 101u] == 0x2222u);
    REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 102u] == 0x3333u);
    REQUIRE(bus.gpu_vram[51u * jojo::PsxBus::gpu_vram_width + 100u] == 0x4444u);
    REQUIRE(bus.gpu_vram[51u * jojo::PsxBus::gpu_vram_width + 101u] == 0x5555u);
    REQUIRE(bus.gpu_vram[51u * jojo::PsxBus::gpu_vram_width + 102u] == 0x6666u);

    // Once all image words are consumed, the next word must be decoded as a
    // fresh GP0 command rather than being treated as leftover image payload.
    gp0(bus, 0x020000ffu);
    gp0(bus, (60u << 16u) | 120u);
    gp0(bus, (1u << 16u) | 2u);
    REQUIRE(bus.gpu_vram[60u * jojo::PsxBus::gpu_vram_width + 120u] == 0x001fu);
    REQUIRE(bus.gpu_vram[60u * jojo::PsxBus::gpu_vram_width + 121u] == 0x001fu);
}

void test_cpu_to_vram_odd_pixel_count_ignores_padding_halfword() {
    jojo::PsxBus bus{};
    gp0(bus, 0xa0000000u);
    gp0(bus, (10u << 16u) | 20u);
    gp0(bus, (1u << 16u) | 3u);
    gp0(bus, 0xbbbbaaaau);
    gp0(bus, 0xddddccccu); // upper DDDD is transport padding for 3 pixels.

    REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 20u] == 0xaaaau);
    REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 21u] == 0xbbbbu);
    REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 22u] == 0xccccu);
    REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 23u] == 0u);
}

void test_draw_environment_and_internal_register_reads() {
    jojo::PsxBus bus{};

    constexpr std::uint32_t draw_mode = 0x05a5u;
    gp0(bus, 0xe1000000u | draw_mode);
    const auto status = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address);
    REQUIRE(status.reason == jojo::PsxBusAccessReason::ok);
    REQUIRE((status.value & 0x07ffu) == draw_mode);

    constexpr std::uint32_t texture_window = 0x054321u & 0x000fffffu;
    gp0(bus, 0xe2000000u | texture_window);
    gp1(bus, 0x10000002u);
    REQUIRE(gpu_read(bus) == texture_window);

    constexpr std::uint32_t draw_top_left = 17u | (33u << 10u);
    gp0(bus, 0xe3000000u | draw_top_left);
    gp1(bus, 0x10000003u);
    REQUIRE(gpu_read(bus) == draw_top_left);

    constexpr std::uint32_t draw_bottom_right = 700u | (400u << 10u);
    gp0(bus, 0xe4000000u | draw_bottom_right);
    gp1(bus, 0x10000004u);
    REQUIRE(gpu_read(bus) == draw_bottom_right);

    constexpr std::uint32_t offset_x = static_cast<std::uint32_t>(-7) & 0x7ffu;
    constexpr std::uint32_t offset_y = 9u & 0x7ffu;
    constexpr std::uint32_t draw_offset = offset_x | (offset_y << 11u);
    gp0(bus, 0xe5000000u | draw_offset);
    gp1(bus, 0x10000005u);
    REQUIRE(gpu_read(bus) == draw_offset);

    gp0(bus, 0xe6000003u);
    const auto masked_status = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address);
    REQUIRE(masked_status.reason == jojo::PsxBusAccessReason::ok);
    REQUIRE(((masked_status.value >> 11u) & 3u) == 3u);
}

void test_mask_setting_applies_to_cpu_to_vram() {
    jojo::PsxBus bus{};
    constexpr std::size_t row = 5u;
    constexpr std::size_t masked_column = 5u;
    constexpr std::size_t force_mask_column = 6u;

    bus.gpu_vram[row * jojo::PsxBus::gpu_vram_width + masked_column] = 0x8001u;

    gp0(bus, 0xe6000002u); // Check old mask bit, do not force new mask bit.
    gp0(bus, 0xa0000000u);
    gp0(bus, (static_cast<std::uint32_t>(row) << 16u) | masked_column);
    gp0(bus, 0x00010001u);
    gp0(bus, 0x00001234u);
    REQUIRE(bus.gpu_vram[row * jojo::PsxBus::gpu_vram_width + masked_column] == 0x8001u);

    gp0(bus, 0xe6000001u); // Draw always and force mask bit on new data.
    gp0(bus, 0xa0000000u);
    gp0(bus, (static_cast<std::uint32_t>(row) << 16u) | force_mask_column);
    gp0(bus, 0x00010001u);
    gp0(bus, 0x00001234u);
    REQUIRE(bus.gpu_vram[row * jojo::PsxBus::gpu_vram_width + force_mask_column] == 0x9234u);
}

void test_untextured_variable_rectangle_applies_offset_clip_and_mask() {
    jojo::PsxBus bus{};

    // Drawing area is inclusive on PS1. The raw vertex (7,10) plus offset
    // (+2,-1) starts at (9,9), so a 5x4 rectangle is clipped to x=10..13,
    // y=10..12 by this drawing area.
    gp0(bus, 0xe3000000u | 10u | (10u << 10u));
    gp0(bus, 0xe4000000u | 13u | (12u << 10u));
    gp0(bus, 0xe5000000u | 2u | ((static_cast<std::uint32_t>(-1) & 0x7ffu) << 11u));

    const auto protected_index = 11u * jojo::PsxBus::gpu_vram_width + 11u;
    bus.gpu_vram[protected_index] = 0x8001u;
    gp0(bus, 0xe6000002u); // Respect old mask bit.

    gp0(bus, 0x600000ffu); // opaque, untextured variable rectangle, bright red
    gp0(bus, vertex(7, 10));
    gp0(bus, (4u << 16u) | 5u);

    REQUIRE(bus.gpu_vram[9u * jojo::PsxBus::gpu_vram_width + 9u] == 0u);
    REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 10u] == 0x001fu);
    REQUIRE(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 13u] == 0x001fu);
    REQUIRE(bus.gpu_vram[11u * jojo::PsxBus::gpu_vram_width + 11u] == 0x8001u);
    REQUIRE(bus.gpu_vram[12u * jojo::PsxBus::gpu_vram_width + 13u] == 0x001fu);
    REQUIRE(bus.gpu_vram[13u * jojo::PsxBus::gpu_vram_width + 10u] == 0u);
}

void test_untextured_fixed_rectangle_sizes_and_force_mask() {
    jojo::PsxBus bus{};
    gp0(bus, 0xe3000000u);
    gp0(bus, 0xe4000000u | 1023u | (511u << 10u));
    gp0(bus, 0xe5000000u);
    gp0(bus, 0xe6000001u); // Force bit15 on rendered pixels.

    gp0(bus, 0x6800ff00u); // untextured 1x1, green
    gp0(bus, vertex(20, 30));
    REQUIRE(bus.gpu_vram[30u * jojo::PsxBus::gpu_vram_width + 20u] == 0x83e0u);
    REQUIRE(bus.gpu_vram[30u * jojo::PsxBus::gpu_vram_width + 21u] == 0u);

    gp0(bus, 0x700000ffu); // untextured 8x8, red
    gp0(bus, vertex(40, 50));
    REQUIRE(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 40u] == 0x801fu);
    REQUIRE(bus.gpu_vram[57u * jojo::PsxBus::gpu_vram_width + 47u] == 0x801fu);
    REQUIRE(bus.gpu_vram[58u * jojo::PsxBus::gpu_vram_width + 47u] == 0u);

    gp0(bus, 0x78ff0000u); // untextured 16x16, blue
    gp0(bus, vertex(60, 70));
    REQUIRE(bus.gpu_vram[70u * jojo::PsxBus::gpu_vram_width + 60u] == 0xfc00u);
    REQUIRE(bus.gpu_vram[85u * jojo::PsxBus::gpu_vram_width + 75u] == 0xfc00u);
    REQUIRE(bus.gpu_vram[86u * jojo::PsxBus::gpu_vram_width + 75u] == 0u);
}

void test_untextured_rectangle_abr_modes() {
    constexpr std::array<std::uint16_t, 4> expected{
        0x1910u, // B/2 + F/2
        0x321fu, // B + F, red clamped to 31
        0x1000u, // B - F, red/green clamped to zero
        0x254eu, // B + F/4
    };

    for (std::uint32_t mode = 0u; mode < expected.size(); ++mode) {
        jojo::PsxBus bus{};
        configure_full_drawing_area(bus);
        gp0(bus, 0xe1000000u | (mode << 5u));

        constexpr std::uint32_t x = 200u;
        constexpr std::uint32_t y = 180u;
        auto& destination = bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + x];
        destination = 0x2108u; // B: R=8, G=8, B=8.

        gp0(bus, 0x6a2040c0u); // semi-transparent 1x1; F: R=24,G=8,B=4.
        gp0(bus, vertex(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)));
        REQUIRE(destination == expected[mode]);
    }
}

void test_textured_semi_transparency_uses_texel_stp() {
    jojo::PsxBus bus{};
    configure_full_drawing_area(bus);

    constexpr std::uint32_t page_x = 64u;
    constexpr std::uint32_t u = 3u;
    constexpr std::uint32_t v = 2u;
    // Page X=64, ABR=B+F, direct 15-bit texture mode.
    gp0(bus, 0xe1000121u);

    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u] = 0x001fu;
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u + 1u] = 0x801fu;
    bus.gpu_vram[v * jojo::PsxBus::gpu_vram_width + page_x + u + 2u] = 0x0000u;

    constexpr std::uint32_t y = 210u;
    auto& opaque_destination = bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + 220u];
    opaque_destination = 0x7c00u;
    gp0(bus, 0x6f000000u); // semi-transparent, raw textured 1x1.
    gp0(bus, vertex(220, static_cast<std::int32_t>(y)));
    gp0(bus, u | (v << 8u));
    REQUIRE(opaque_destination == 0x001fu); // STP=0: draw opaque even for semi command.

    auto& blended_destination = bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + 221u];
    blended_destination = 0x7c00u;
    gp0(bus, 0x6f000000u);
    gp0(bus, vertex(221, static_cast<std::int32_t>(y)));
    gp0(bus, (u + 1u) | (v << 8u));
    REQUIRE(blended_destination == 0xfc1fu); // STP=1: B+F and retain texture bit15.

    auto& transparent_destination = bus.gpu_vram[y * jojo::PsxBus::gpu_vram_width + 222u];
    transparent_destination = 0x1234u;
    gp0(bus, 0x6f000000u);
    gp0(bus, vertex(222, static_cast<std::int32_t>(y)));
    gp0(bus, (u + 2u) | (v << 8u));
    REQUIRE(transparent_destination == 0x1234u); // 0000h remains fully transparent.
}

struct GpuContractRunner {
    GpuContractRunner() {
        test_cpu_to_vram_upload_and_parser_recovery();
        test_cpu_to_vram_odd_pixel_count_ignores_padding_halfword();
        test_draw_environment_and_internal_register_reads();
        test_mask_setting_applies_to_cpu_to_vram();
        test_untextured_variable_rectangle_applies_offset_clip_and_mask();
        test_untextured_fixed_rectangle_sizes_and_force_mask();
        test_untextured_rectangle_abr_modes();
        test_textured_semi_transparency_uses_texel_stp();
    }
};

GpuContractRunner gpu_contract_runner{};

} // namespace
