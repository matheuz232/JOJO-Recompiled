#include "core/psx_bus.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void sync_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d timer sync contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define SYNC_REQUIRE(expr) do { if (!(expr)) sync_fail(#expr, __LINE__); } while (0)

void write16(jojo::PsxBus& bus, std::uint32_t address, std::uint16_t value) {
    SYNC_REQUIRE(jojo::psx_bus_write_u16(bus, address, value) ==
                 jojo::PsxBusAccessReason::ok);
}

std::uint16_t read16(jojo::PsxBus& bus, std::uint32_t address) {
    const auto value = jojo::psx_bus_read_u16(bus, address);
    SYNC_REQUIRE(value.reason == jojo::PsxBusAccessReason::ok);
    return value.value;
}

void test_timer2_sync_stop_and_free_run_modes() {
    jojo::PsxBus bus{};

    write16(bus, jojo::PsxBus::timer2_mode_address, 0x0001u); // sync mode 0: stop
    jojo::psx_bus_tick(bus, 32u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer2_current_address) == 0u);

    write16(bus, jojo::PsxBus::timer2_mode_address, 0x0007u); // sync mode 3: stop
    jojo::psx_bus_tick(bus, 32u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer2_current_address) == 0u);

    write16(bus, jojo::PsxBus::timer2_mode_address, 0x0003u); // sync mode 1: free-run
    jojo::psx_bus_tick(bus, 9u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer2_current_address) == 9u);

    write16(bus, jojo::PsxBus::timer2_mode_address, 0x0205u); // sync mode 2 + sys/8
    jojo::psx_bus_tick(bus, 7u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer2_current_address) == 0u);
    jojo::psx_bus_tick(bus, 1u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer2_current_address) == 1u);
}

void test_timer0_sync1_resets_on_hblank() {
    jojo::PsxBus bus{};
    write16(bus, jojo::PsxBus::timer0_mode_address, 0x0003u); // sync mode 1, sysclk

    jojo::psx_bus_tick(bus, 100u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer0_current_address) == 100u);

    // NTSC HBlank arrives after roughly 2153 CPU cycles. The 100 cycles above
    // are already part of the physical GPU phase, so another 2053 crosses it.
    jojo::psx_bus_tick(bus, 2053u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer0_current_address) == 0u);
}

void test_timer0_sync3_unlocks_after_first_hblank() {
    jojo::PsxBus bus{};
    write16(bus, jojo::PsxBus::timer0_mode_address, 0x0007u); // sync mode 3

    jojo::psx_bus_tick(bus, 2152u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer0_current_address) == 0u);
    jojo::psx_bus_tick(bus, 1u); // first HBlank unlocks free-run
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer0_current_address) == 0u);
    jojo::psx_bus_tick(bus, 10u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer0_current_address) == 10u);
}

void test_timer1_sync1_resets_on_vblank() {
    jojo::PsxBus bus{};
    write16(bus, jojo::PsxBus::timer1_mode_address, 0x0003u); // sync mode 1, sysclk
    write16(bus, jojo::PsxBus::timer1_current_address, 50u);

    // Put the physical GPU one scanline before NTSC VBlank. The next HBlank
    // enters line 240 and must reset Timer1.
    bus.gpu_scanline = 239u;
    jojo::psx_bus_tick(bus, 2153u);
    SYNC_REQUIRE(bus.gpu_scanline == 240u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer1_current_address) == 0u);
}

void test_timer1_sync3_unlocks_after_first_vblank() {
    jojo::PsxBus bus{};
    write16(bus, jojo::PsxBus::timer1_mode_address, 0x0007u); // sync mode 3

    jojo::psx_bus_tick(bus, 100u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer1_current_address) == 0u);

    bus.gpu_scanline = 239u;
    jojo::psx_bus_tick(bus, 2153u); // first VBlank unlocks free-run
    SYNC_REQUIRE(bus.gpu_scanline == 240u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer1_current_address) == 0u);
    jojo::psx_bus_tick(bus, 10u);
    SYNC_REQUIRE(read16(bus, jojo::PsxBus::timer1_current_address) == 10u);
}

struct TimerSyncRunner {
    TimerSyncRunner() {
        test_timer2_sync_stop_and_free_run_modes();
        test_timer0_sync1_resets_on_hblank();
        test_timer0_sync3_unlocks_after_first_hblank();
        test_timer1_sync1_resets_on_vblank();
        test_timer1_sync3_unlocks_after_first_vblank();
    }
};

TimerSyncRunner timer_sync_runner{};

} // namespace
