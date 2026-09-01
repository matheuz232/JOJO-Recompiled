#include "core/psx_bus.h"

#include <array>
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

struct TimerCase {
    std::uint32_t current;
    std::uint32_t mode;
    std::uint32_t target;
    std::uint16_t irq_bit;
};

constexpr std::array<TimerCase, 3> timers{{
    {timer0_current, timer0_mode, timer0_target, static_cast<std::uint16_t>(1u << 4u)},
    {timer1_current, timer1_mode, timer1_target, static_cast<std::uint16_t>(1u << 5u)},
    {timer2_current, timer2_mode, timer2_target, static_cast<std::uint16_t>(1u << 6u)},
}};

void require_write16(jojo::PsxBus& bus, std::uint32_t address, std::uint16_t value) {
    TIMER_REQUIRE(jojo::psx_bus_write_u16(bus, address, value) ==
                  jojo::PsxBusAccessReason::ok);
}

std::uint16_t read16(jojo::PsxBus& bus, std::uint32_t address) {
    const auto result = jojo::psx_bus_read_u16(bus, address);
    TIMER_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

std::uint32_t read32(jojo::PsxBus& bus, std::uint32_t address) {
    const auto result = jojo::psx_bus_read_u32(bus, address);
    TIMER_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

void acknowledge_irq(jojo::PsxBus& bus, std::uint16_t irq_bit) {
    const auto keep = static_cast<std::uint16_t>(
        jojo::PsxBus::interrupt_status_valid_bits & ~irq_bit);
    require_write16(bus, jojo::PsxBus::interrupt_status_address, keep);
    TIMER_REQUIRE((bus.interrupt_status & irq_bit) == 0u);
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

void test_target_reset_and_repeated_irq_for_all_root_counters() {
    constexpr std::uint16_t reset_at_target = 1u << 3u;
    constexpr std::uint16_t irq_at_target = 1u << 4u;
    constexpr std::uint16_t repeat_irq = 1u << 6u;
    constexpr std::uint16_t mode = reset_at_target | irq_at_target | repeat_irq;

    for (const auto& timer : timers) {
        jojo::PsxBus bus{};
        require_write16(bus, timer.target, 3u);
        require_write16(bus, timer.mode, mode);

        jojo::psx_bus_tick(bus, 2u);
        TIMER_REQUIRE(read16(bus, timer.current) == 2u);
        TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) == 0u);

        jojo::psx_bus_tick(bus, 1u);
        TIMER_REQUIRE(read16(bus, timer.current) == 3u);
        TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) != 0u);

        acknowledge_irq(bus, timer.irq_bit);
        jojo::psx_bus_tick(bus, 1u);
        TIMER_REQUIRE(read16(bus, timer.current) == 0u);
        TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) == 0u);

        jojo::psx_bus_tick(bus, 3u);
        TIMER_REQUIRE(read16(bus, timer.current) == 3u);
        TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) != 0u);
    }
}

void test_ffff_wrap_and_irq() {
    constexpr std::uint16_t irq_at_ffff = 1u << 5u;
    constexpr std::uint16_t repeat_irq = 1u << 6u;

    for (const auto& timer : timers) {
        jojo::PsxBus bus{};
        require_write16(bus, timer.mode, irq_at_ffff | repeat_irq);
        require_write16(bus, timer.current, 0xfffeu);

        jojo::psx_bus_tick(bus, 1u);
        TIMER_REQUIRE(read16(bus, timer.current) == 0xffffu);
        TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) != 0u);

        acknowledge_irq(bus, timer.irq_bit);
        jojo::psx_bus_tick(bus, 1u);
        TIMER_REQUIRE(read16(bus, timer.current) == 0u);
        TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) == 0u);
    }
}

void test_one_shot_irq_rearms_only_on_mode_write() {
    constexpr std::uint16_t reset_at_target = 1u << 3u;
    constexpr std::uint16_t irq_at_target = 1u << 4u;
    constexpr std::uint16_t one_shot_mode = reset_at_target | irq_at_target;

    jojo::PsxBus bus{};
    const auto& timer = timers[0];
    require_write16(bus, timer.target, 2u);
    require_write16(bus, timer.mode, one_shot_mode);

    jojo::psx_bus_tick(bus, 2u);
    TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) != 0u);
    acknowledge_irq(bus, timer.irq_bit);

    jojo::psx_bus_tick(bus, 1u); // target reset -> zero
    jojo::psx_bus_tick(bus, 2u); // reaches target again
    TIMER_REQUIRE(read16(bus, timer.current) == 2u);
    TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) == 0u);

    require_write16(bus, timer.mode, one_shot_mode);
    jojo::psx_bus_tick(bus, 2u);
    TIMER_REQUIRE((bus.interrupt_status & timer.irq_bit) != 0u);
}

void test_mode_status_bits_and_clear_on_read() {
    constexpr std::uint16_t irq_request_idle = 1u << 10u;
    constexpr std::uint16_t reached_target = 1u << 11u;
    constexpr std::uint16_t reached_ffff = 1u << 12u;

    for (const auto& timer : timers) {
        jojo::PsxBus bus{};
        require_write16(bus, timer.mode, 0u);
        const auto after_write = read16(bus, timer.mode);
        TIMER_REQUIRE((after_write & irq_request_idle) != 0u);
        TIMER_REQUIRE((after_write & (reached_target | reached_ffff)) == 0u);

        require_write16(bus, timer.target, 2u);
        jojo::psx_bus_tick(bus, 2u);
        const auto target_first = read16(bus, timer.mode);
        TIMER_REQUIRE((target_first & reached_target) != 0u);
        TIMER_REQUIRE((target_first & reached_ffff) == 0u);
        TIMER_REQUIRE((target_first & irq_request_idle) != 0u);
        const auto target_second = read16(bus, timer.mode);
        TIMER_REQUIRE((target_second & reached_target) == 0u);

        require_write16(bus, timer.current, 0xfffeu);
        jojo::psx_bus_tick(bus, 1u);
        const auto ffff_first = read32(bus, timer.mode);
        TIMER_REQUIRE((ffff_first & reached_ffff) != 0u);
        TIMER_REQUIRE((ffff_first & irq_request_idle) != 0u);
        const auto ffff_second = read32(bus, timer.mode);
        TIMER_REQUIRE((ffff_second & reached_ffff) == 0u);
    }
}

struct TimerFoundationRunner {
    TimerFoundationRunner() {
        test_all_root_counter_registers_are_mapped();
        test_system_clock_free_run_advances_timer0_and_timer1();
        test_timer2_system_clock_and_div8_preserve_phase();
        test_target_reset_and_repeated_irq_for_all_root_counters();
        test_ffff_wrap_and_irq();
        test_one_shot_irq_rearms_only_on_mode_write();
        test_mode_status_bits_and_clear_on_read();
    }
};

TimerFoundationRunner timer_foundation_runner{};

} // namespace
