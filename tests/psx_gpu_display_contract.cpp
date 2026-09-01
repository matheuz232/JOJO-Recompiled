#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void display_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d GPU display contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define DISPLAY_REQUIRE(expr) do { if (!(expr)) display_fail(#expr, __LINE__); } while (0)

void gp0(jojo::PsxBus& bus, std::uint32_t value) {
    DISPLAY_REQUIRE(jojo::psx_bus_write_u32(
                        bus, jojo::PsxBus::gpu_gp0_address, value) ==
                    jojo::PsxBusAccessReason::ok);
}

void gp1(jojo::PsxBus& bus, std::uint32_t value) {
    DISPLAY_REQUIRE(jojo::psx_bus_write_u32(
                        bus, jojo::PsxBus::gpu_gp1_address, value) ==
                    jojo::PsxBusAccessReason::ok);
}

std::uint32_t status(const jojo::PsxBus& bus) {
    const auto read = jojo::psx_bus_read_u32(bus, jojo::PsxBus::gpu_gp1_address);
    DISPLAY_REQUIRE(read.reason == jojo::PsxBusAccessReason::ok);
    return read.value;
}

void test_reset_display_defaults() {
    jojo::PsxBus bus{};

    DISPLAY_REQUIRE(bus.gpu_display_vram_start == 0u);
    DISPLAY_REQUIRE(bus.gpu_horizontal_display_range ==
                    (0x200u | ((0x200u + 256u * 10u) << 12u)));
    DISPLAY_REQUIRE(bus.gpu_vertical_display_range ==
                    (0x010u | ((0x010u + 240u) << 10u)));
    DISPLAY_REQUIRE(bus.gpu_display_mode == 0u);

    gp1(bus, 0x0507ffffu);
    gp1(bus, 0x06abcdefu);
    gp1(bus, 0x070abcdeu);
    gp1(bus, 0x0800007fu);
    gp1(bus, 0x00000000u);

    DISPLAY_REQUIRE(bus.gpu_display_vram_start == 0u);
    DISPLAY_REQUIRE(bus.gpu_horizontal_display_range ==
                    (0x200u | ((0x200u + 256u * 10u) << 12u)));
    DISPLAY_REQUIRE(bus.gpu_vertical_display_range ==
                    (0x010u | ((0x010u + 240u) << 10u)));
    DISPLAY_REQUIRE(bus.gpu_display_mode == 0u);
    DISPLAY_REQUIRE(status(bus) == jojo::PsxBus::gpu_status_reset);
}

void test_display_start_and_ranges_are_masked() {
    jojo::PsxBus bus{};

    gp1(bus, 0x05ffffffu);
    DISPLAY_REQUIRE(bus.gpu_display_vram_start == 0x0007ffffu);

    gp1(bus, 0x06abcdefu);
    DISPLAY_REQUIRE(bus.gpu_horizontal_display_range == 0x00abcdefu);

    gp1(bus, 0x07ffffffu);
    DISPLAY_REQUIRE(bus.gpu_vertical_display_range == 0x000fffffu);
}

void test_display_mode_updates_gpustat_bits_16_through_22() {
    jojo::PsxBus bus{};
    constexpr std::uint32_t mode_bits = 0x6du; // res2 + 480 + PAL + interlace + res1=1
    gp1(bus, 0x08000000u | mode_bits);

    DISPLAY_REQUIRE(bus.gpu_display_mode == mode_bits);
    constexpr std::uint32_t expected_status_bits =
        (1u << 16u) | // horizontal resolution 2
        (1u << 17u) | // horizontal resolution 1 = 1
        (1u << 19u) | // vertical resolution
        (1u << 20u) | // PAL
        (0u << 21u) | // 15-bit display
        (1u << 22u);  // interlace
    DISPLAY_REQUIRE((status(bus) & 0x007f0000u) == expected_status_bits);
}

void test_reset_command_buffer_aborts_partial_gp0_and_polyline() {
    jojo::PsxBus bus{};

    gp0(bus, 0xa0000000u);
    gp0(bus, 0x00000000u);
    DISPLAY_REQUIRE(bus.gpu_gp0_packet_count != 0u);
    gp1(bus, 0x01000000u);
    DISPLAY_REQUIRE(bus.gpu_gp0_packet_count == 0u);
    DISPLAY_REQUIRE(bus.gpu_gp0_packet_words == 0u);

    gp0(bus, 0xe3000000u);
    gp0(bus, 0xe4000000u | 1023u | (511u << 10u));
    gp0(bus, 0x480000ffu);
    gp0(bus, (10u << 16u) | 10u);
    gp0(bus, (10u << 16u) | 12u);
    DISPLAY_REQUIRE(bus.gpu_polyline_active);
    gp1(bus, 0x01000000u);
    DISPLAY_REQUIRE(!bus.gpu_polyline_active);
    DISPLAY_REQUIRE(!bus.gpu_polyline_gouraud);
    DISPLAY_REQUIRE(!bus.gpu_polyline_expect_color);
}

void test_gpu_irq_request_and_acknowledge() {
    jojo::PsxBus bus{};
    DISPLAY_REQUIRE((status(bus) & (1u << 24u)) == 0u);

    gp0(bus, 0x1f000000u);
    DISPLAY_REQUIRE((status(bus) & (1u << 24u)) != 0u);

    gp1(bus, 0x02000000u);
    DISPLAY_REQUIRE((status(bus) & (1u << 24u)) == 0u);
}

struct DisplayContractRunner {
    DisplayContractRunner() {
        test_reset_display_defaults();
        test_display_start_and_ranges_are_masked();
        test_display_mode_updates_gpustat_bits_16_through_22();
        test_reset_command_buffer_aborts_partial_gp0_and_polyline();
        test_gpu_irq_request_and_acknowledge();
    }
};

DisplayContractRunner display_contract_runner{};

} // namespace
