#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void delay_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d timer delay contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define DELAY_REQUIRE(expr) do { if (!(expr)) delay_fail(#expr, __LINE__); } while (0)

constexpr std::uint32_t timer0_current = 0x1f801100u;
constexpr std::uint32_t timer0_mode = 0x1f801104u;
constexpr std::uint32_t timer0_target = 0x1f801108u;

void write16(jojo::PsxBus& bus, std::uint32_t address, std::uint16_t value) {
    DELAY_REQUIRE(jojo::psx_bus_write_u16(bus, address, value) ==
                  jojo::PsxBusAccessReason::ok);
}

std::uint16_t current(jojo::PsxBus& bus) {
    const auto value = jojo::psx_bus_read_u16(bus, timer0_current);
    DELAY_REQUIRE(value.reason == jojo::PsxBusAccessReason::ok);
    return value.value;
}

void test_mode_write_holds_zero_for_two_clock_cycles() {
    jojo::PsxBus bus{};
    write16(bus, timer0_mode, 0u);
    DELAY_REQUIRE(current(bus) == 0u); // write cycle

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0u); // second held cycle

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 1u);
}

void test_current_write_holds_written_value_for_two_clock_cycles() {
    jojo::PsxBus bus{};
    write16(bus, timer0_mode, 0u);
    jojo::psx_bus_tick(bus, 2u); // settle MODE-write hold

    write16(bus, timer0_current, 5u);
    DELAY_REQUIRE(current(bus) == 5u); // write cycle

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 5u); // second held cycle

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 6u);
}

void test_target_reset_holds_zero_for_two_clock_cycles() {
    constexpr std::uint16_t reset_at_target = 1u << 3u;

    jojo::PsxBus bus{};
    write16(bus, timer0_target, 1u);
    write16(bus, timer0_mode, reset_at_target);

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0u); // MODE-write hold
    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 1u); // target reached

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0u); // first reset cycle
    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0u); // second reset cycle
    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 1u);
}

void test_ffff_wrap_holds_zero_for_one_clock_cycle() {
    jojo::PsxBus bus{};
    write16(bus, timer0_mode, 0u);
    jojo::psx_bus_tick(bus, 2u); // settle MODE-write hold

    write16(bus, timer0_current, 0xfffeu);
    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0xfffeu); // CURRENT-write hold
    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0xffffu);

    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 0u); // one wrapped-zero cycle
    jojo::psx_bus_tick(bus, 1u);
    DELAY_REQUIRE(current(bus) == 1u);
}

struct TimerDelayRunner {
    TimerDelayRunner() {
        test_mode_write_holds_zero_for_two_clock_cycles();
        test_current_write_holds_written_value_for_two_clock_cycles();
        test_target_reset_holds_zero_for_two_clock_cycles();
        test_ffff_wrap_holds_zero_for_one_clock_cycle();
    }
};

TimerDelayRunner timer_delay_runner{};

} // namespace
