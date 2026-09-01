#include "core/psx_bus.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void dotclock_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d timer dotclock contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define DOTCLOCK_REQUIRE(expr) do { if (!(expr)) dotclock_fail(#expr, __LINE__); } while (0)

void write_gp1(jojo::PsxBus& bus, std::uint32_t value) {
    DOTCLOCK_REQUIRE(jojo::psx_bus_write_u32(
        bus, jojo::PsxBus::gpu_gp1_address, value) == jojo::PsxBusAccessReason::ok);
}

void write_timer0_mode(jojo::PsxBus& bus, std::uint16_t value) {
    DOTCLOCK_REQUIRE(jojo::psx_bus_write_u16(
        bus, jojo::PsxBus::timer0_mode_address, value) == jojo::PsxBusAccessReason::ok);
}

std::uint16_t timer0(jojo::PsxBus& bus) {
    const auto value = jojo::psx_bus_read_u16(bus, jojo::PsxBus::timer0_current_address);
    DOTCLOCK_REQUIRE(value.reason == jojo::PsxBusAccessReason::ok);
    return value.value;
}

void test_timer0_dotclock_tracks_horizontal_resolution() {
    struct Case {
        std::uint8_t display_mode;
        std::uint16_t expected_dots;
    };
    constexpr std::array<Case, 5> cases{{
        {0x00u, 341u}, // 256px: 3413 / 10
        {0x01u, 426u}, // 320px: 3413 / 8
        {0x40u, 487u}, // 368px: 3413 / 7
        {0x02u, 682u}, // 512px: 3413 / 5
        {0x03u, 853u}, // 640px: 3413 / 4
    }};

    for (const auto& c : cases) {
        jojo::PsxBus bus{};
        write_gp1(bus, 0x08000000u | c.display_mode);
        write_timer0_mode(bus, 0x0100u); // clock source 1 = dotclock, free-run
        jojo::psx_bus_tick(bus, 2153u);
        DOTCLOCK_REQUIRE(timer0(bus) == c.expected_dots);
    }
}

void test_timer0_clock_source_three_is_dotclock_too() {
    jojo::PsxBus bus{};
    write_gp1(bus, 0x08000001u); // 320px
    write_timer0_mode(bus, 0x0300u); // clock source 3 = dotclock, free-run
    jojo::psx_bus_tick(bus, 2153u);
    DOTCLOCK_REQUIRE(timer0(bus) == 426u);
}

struct TimerDotclockRunner {
    TimerDotclockRunner() {
        test_timer0_dotclock_tracks_horizontal_resolution();
        test_timer0_clock_source_three_is_dotclock_too();
    }
};

TimerDotclockRunner timer_dotclock_runner{};

} // namespace
