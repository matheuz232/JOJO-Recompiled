#include "core/psx_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

namespace {

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    CHECK(jojo::psx_bus_write_u32(bus, jojo::PsxBus::gpu_gp0_address, value) ==
          jojo::PsxBusAccessReason::ok);
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

    CHECK(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 100u] == 0x1111u);
    CHECK(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 101u] == 0x2222u);
    CHECK(bus.gpu_vram[50u * jojo::PsxBus::gpu_vram_width + 102u] == 0x3333u);
    CHECK(bus.gpu_vram[51u * jojo::PsxBus::gpu_vram_width + 100u] == 0x4444u);
    CHECK(bus.gpu_vram[51u * jojo::PsxBus::gpu_vram_width + 101u] == 0x5555u);
    CHECK(bus.gpu_vram[51u * jojo::PsxBus::gpu_vram_width + 102u] == 0x6666u);

    // Once all image words are consumed, the next word must be decoded as a
    // fresh GP0 command rather than being treated as leftover image payload.
    gp0(bus, 0x020000ffu);
    gp0(bus, (60u << 16u) | 120u);
    gp0(bus, (1u << 16u) | 2u);
    CHECK(bus.gpu_vram[60u * jojo::PsxBus::gpu_vram_width + 120u] == 0x001fu);
    CHECK(bus.gpu_vram[60u * jojo::PsxBus::gpu_vram_width + 121u] == 0x001fu);
}

void test_cpu_to_vram_odd_pixel_count_ignores_padding_halfword() {
    jojo::PsxBus bus{};
    gp0(bus, 0xa0000000u);
    gp0(bus, (10u << 16u) | 20u);
    gp0(bus, (1u << 16u) | 3u);
    gp0(bus, 0xbbbbaaaau);
    gp0(bus, 0xddddccccu); // upper DDDD is transport padding for 3 pixels.

    CHECK(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 20u] == 0xaaaau);
    CHECK(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 21u] == 0xbbbbu);
    CHECK(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 22u] == 0xccccu);
    CHECK(bus.gpu_vram[10u * jojo::PsxBus::gpu_vram_width + 23u] == 0u);
}

} // namespace

int main() {
    test_cpu_to_vram_upload_and_parser_recovery();
    test_cpu_to_vram_odd_pixel_count_ignores_padding_halfword();
    if (failures != 0) return 1;
    std::cout << "PS1 GPU image-load assertions passed\n";
    return 0;
}
