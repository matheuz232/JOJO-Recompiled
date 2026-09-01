#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void timer_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d timer foundation failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define TIMER_REQUIRE(expr) do { if (!(expr)) timer_fail(#expr, __LINE__); } while (0)

constexpr std::uint32_t timer0_current = 0x1f801100u;
constexpr std::uint32_t timer0_mode = 0x1f801104u;
constexpr std::uint32_t timer0_target = 0x1f801108u;
constexpr std::uint32_t timer1_current = 0x1f801110u;
constexpr std::uint32_t timer1_mode = 0x1f801114u;
constexpr std::uint32_t timer1_target = 0x1f801118u;
constexpr std::uint32_t timer2_current = 0x1f801120u;
constexpr std::uint32_t timer2_mode = 0x1f801124u;
constexpr std::uint32_t timer2_target = 0x1f801128u;

void require_write16(jojo::PsxBus& bus, std::uint32_t address, std::uint16_t value) {
    TIMER_REQUIRE(jojo::psx_bus_write_u16(bus, address, value) ==
                  jojo::PsxBusAccessReason::ok);
}

std::uint16_t read16(jojo::PsxBus& bus, std::uint32_t address) {
    const auto result = jojo::psx_bus_read_u16(bus, address);
    TIMER_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

void test_all_root_counter_registers_are_mapped() {
    jojo::PsxBus bus{};

    require_write16(bus, timer0_current, 0x1234u);
    require_write16(bus, timer0_target, 0x2345u);
    TIMER_REQUIRE(read16(bus, timer0_current) == 0x1234u);
    TIMER_REQUIRE(read16(bus, timer0_target) == 0x2345u);
    require_write16(bus, timer0_mode, 0x0000u);
    TIMER_REQUIRE(read16(bus, timer0_current) == 0u);
    TIMER_REQUIRE((read16(bus, timer0_mode) & 0x03ffu) == 0u);

    require_write16(bus, timer1_target, 0x3456u);
    TIMER_REQUIRE(read16(bus, timer1_target) == 0x3456u);

    require_write16(bus, timer2_current, 0x4567u);
    require_write16(bus, timer2_target, 0x5678u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 0x4567u);
    TIMER_REQUIRE(read16(bus, timer2_target) == 0x5678u);
    require_write16(bus, timer2_mode, 0x0000u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 0u);
    TIMER_REQUIRE((read16(bus, timer2_mode) & 0x03ffu) == 0u);
}

void test_system_clock_free_run_advances_timer0_and_timer1() {
    jojo::PsxBus bus{};
    require_write16(bus, timer0_mode, 0x0000u); // system clock
    require_write16(bus, timer1_mode, 0x0000u); // system clock

    jojo::psx_bus_tick(bus, 5u);
    TIMER_REQUIRE(read16(bus, timer0_current) == 5u);
    TIMER_REQUIRE(read16(bus, timer1_current) == 5u);

    jojo::psx_bus_tick(bus, 9u);
    TIMER_REQUIRE(read16(bus, timer0_current) == 14u);
    TIMER_REQUIRE(read16(bus, timer1_current) == 14u);
}

void test_timer2_system_clock_and_div8_preserve_phase() {
    jojo::PsxBus bus{};

    require_write16(bus, timer2_mode, 0x0000u); // system clock
    jojo::psx_bus_tick(bus, 3u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 3u);

    require_write16(bus, timer2_mode, 0x0200u); // system clock / 8
    TIMER_REQUIRE(read16(bus, timer2_current) == 0u);
    jojo::psx_bus_tick(bus, 7u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 0u);
    jojo::psx_bus_tick(bus, 1u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 1u);
    jojo::psx_bus_tick(bus, 15u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 2u);
    jojo::psx_bus_tick(bus, 1u);
    TIMER_REQUIRE(read16(bus, timer2_current) == 3u);
}

struct TimerFoundationRunner {
    TimerFoundationRunner() {
        test_all_root_counter_registers_are_mapped();
        test_system_clock_free_run_advances_timer0_and_timer1();
        test_timer2_system_clock_and_div8_preserve_phase();
    }
};

TimerFoundationRunner timer_foundation_runner{};

} // namespace
