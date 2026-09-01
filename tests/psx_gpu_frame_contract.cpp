#include "core/psx_video.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void frame_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d PSX frame contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define FRAME_REQUIRE(expr) do { if (!(expr)) frame_fail(#expr, __LINE__); } while (0)

void gp1(jojo::PsxBus& bus, std::uint32_t value) {
    FRAME_REQUIRE(jojo::psx_bus_write_u32(
                      bus, jojo::PsxBus::gpu_gp1_address, value) ==
                  jojo::PsxBusAccessReason::ok);
}

std::uint32_t rgba_at(const jojo::PsxVideoFrame& frame,
                      std::uint32_t x,
                      std::uint32_t y) {
    return frame.rgba[static_cast<std::size_t>(y) * frame.width + x];
}

void test_disabled_display_captures_black_default_surface() {
    jojo::PsxBus bus{};
    bus.gpu_vram[0] = 0x001fu;

    auto frame = jojo::capture_psx_video_frame(bus);
    FRAME_REQUIRE(frame);
    FRAME_REQUIRE(!frame.value.display_enabled);
    FRAME_REQUIRE(frame.value.width == 256u);
    FRAME_REQUIRE(frame.value.height == 240u);
    FRAME_REQUIRE(frame.value.rgba.size() == 256u * 240u);
    FRAME_REQUIRE(rgba_at(frame.value, 0u, 0u) == 0xff000000u);
}

void test_320x240_15bit_frame_uses_display_start_and_bgr555_conversion() {
    jojo::PsxBus bus{};
    gp1(bus, 0x03000000u); // display enable
    gp1(bus, 0x08000001u); // 320x240, NTSC, 15-bit
    gp1(bus, 0x05000000u | 10u | (20u << 10u));

    bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 10u] = 0x001fu; // red
    bus.gpu_vram[20u * jojo::PsxBus::gpu_vram_width + 11u] = 0x03e0u; // green
    bus.gpu_vram[21u * jojo::PsxBus::gpu_vram_width + 10u] = 0x7c00u; // blue

    auto frame = jojo::capture_psx_video_frame(bus);
    FRAME_REQUIRE(frame);
    FRAME_REQUIRE(frame.value.display_enabled);
    FRAME_REQUIRE(!frame.value.rgb24);
    FRAME_REQUIRE(frame.value.width == 320u);
    FRAME_REQUIRE(frame.value.height == 240u);
    FRAME_REQUIRE(rgba_at(frame.value, 0u, 0u) == 0xff0000ffu);
    FRAME_REQUIRE(rgba_at(frame.value, 1u, 0u) == 0xff00ff00u);
    FRAME_REQUIRE(rgba_at(frame.value, 0u, 1u) == 0xffff0000u);
}

void test_frame_source_wraps_vram_edges() {
    jojo::PsxBus bus{};
    gp1(bus, 0x03000000u);
    gp1(bus, 0x08000000u); // 256x240
    gp1(bus, 0x05000000u | 1023u | (511u << 10u));

    bus.gpu_vram[511u * jojo::PsxBus::gpu_vram_width + 1023u] = 0x7fffu;
    bus.gpu_vram[511u * jojo::PsxBus::gpu_vram_width + 0u] = 0x001fu;
    bus.gpu_vram[0u * jojo::PsxBus::gpu_vram_width + 1023u] = 0x03e0u;

    auto frame = jojo::capture_psx_video_frame(bus);
    FRAME_REQUIRE(frame);
    FRAME_REQUIRE(rgba_at(frame.value, 0u, 0u) == 0xffffffffu);
    FRAME_REQUIRE(rgba_at(frame.value, 1u, 0u) == 0xff0000ffu);
    FRAME_REQUIRE(rgba_at(frame.value, 0u, 1u) == 0xff00ff00u);
}

void test_resolution_modes_cover_368_and_480_lines() {
    jojo::PsxBus bus{};
    gp1(bus, 0x03000000u);
    gp1(bus, 0x08000044u); // res2=368 and vertical=480

    auto frame = jojo::capture_psx_video_frame(bus);
    FRAME_REQUIRE(frame);
    FRAME_REQUIRE(frame.value.width == 368u);
    FRAME_REQUIRE(frame.value.height == 480u);
}

void test_24bit_display_is_explicitly_not_faked_yet() {
    jojo::PsxBus bus{};
    gp1(bus, 0x03000000u);
    gp1(bus, 0x08000010u);

    const auto frame = jojo::capture_psx_video_frame(bus);
    FRAME_REQUIRE(!frame);
    FRAME_REQUIRE(frame.error == jojo::ErrorCode::backend_unavailable);
}

struct FrameContractRunner {
    FrameContractRunner() {
        test_disabled_display_captures_black_default_surface();
        test_320x240_15bit_frame_uses_display_start_and_bgr555_conversion();
        test_frame_source_wraps_vram_edges();
        test_resolution_modes_cover_368_and_480_lines();
        test_24bit_display_is_explicitly_not_faked_yet();
    }
};

FrameContractRunner frame_contract_runner{};

} // namespace
