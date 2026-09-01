#include "core/psx_runtime.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void bios_timer_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d BIOS timer contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define BIOS_TIMER_REQUIRE(expr) do { if (!(expr)) bios_timer_fail(#expr, __LINE__); } while (0)

std::uint32_t call_b0(jojo::PsxRuntime& runtime,
                      std::uint32_t function,
                      std::uint32_t a0 = 0u,
                      std::uint32_t a1 = 0u,
                      std::uint32_t a2 = 0u) {
    constexpr std::uint32_t return_pc = 0x80012000u;
    runtime.cpu.pc = 0x000000b0u;
    runtime.cpu.next_pc = 0x000000b4u;
    runtime.cpu.gpr[4] = a0;
    runtime.cpu.gpr[5] = a1;
    runtime.cpu.gpr[6] = a2;
    runtime.cpu.gpr[9] = function;
    runtime.cpu.gpr[31] = return_pc;
    const auto result = jojo::step_psx_runtime(runtime);
    BIOS_TIMER_REQUIRE(result.reason == jojo::PsxR3000aStepReason::ok);
    BIOS_TIMER_REQUIRE(runtime.cpu.pc == return_pc);
    BIOS_TIMER_REQUIRE(runtime.cpu.next_pc == return_pc + 4u);
    return runtime.cpu.gpr[2];
}

std::uint16_t read16(jojo::PsxRuntime& runtime, std::uint32_t address) {
    const auto result = jojo::psx_bus_read_u16(runtime.bus, address);
    BIOS_TIMER_REQUIRE(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

void write16(jojo::PsxRuntime& runtime, std::uint32_t address, std::uint16_t value) {
    BIOS_TIMER_REQUIRE(jojo::psx_bus_write_u16(runtime.bus, address, value) ==
                       jojo::PsxBusAccessReason::ok);
}

void test_b02_init_timer_programs_root_counters() {
    struct Case {
        std::uint32_t timer;
        std::uint16_t reload;
        std::uint32_t flags;
        std::uint16_t expected_control;
    };
    constexpr std::array<Case, 3> cases{{
        {0u, 0x1234u, 0x0000u, 0x0148u},
        {1u, 0x2345u, 0x0010u, 0x0149u},
        {2u, 0x3456u, 0x1001u, 0x0058u},
    }};

    for (const auto& item : cases) {
        jojo::PsxRuntime runtime{};
        const auto current = jojo::PsxBus::timer0_current_address + item.timer * 0x10u;
        const auto mode = jojo::PsxBus::timer0_mode_address + item.timer * 0x10u;
        const auto target = jojo::PsxBus::timer0_target_address + item.timer * 0x10u;
        write16(runtime, current, 0xaaaau);
        write16(runtime, target, 0xbbbbu);

        BIOS_TIMER_REQUIRE(call_b0(runtime, 0x02u, item.timer, item.reload, item.flags) == 1u);
        BIOS_TIMER_REQUIRE(read16(runtime, current) == 0u);
        BIOS_TIMER_REQUIRE(read16(runtime, target) == item.reload);
        const auto programmed = read16(runtime, mode);
        BIOS_TIMER_REQUIRE((programmed & 0x03ffu) == item.expected_control);
        BIOS_TIMER_REQUIRE((programmed & jojo::PsxBus::timer_mode_interrupt_request) != 0u);
    }

    jojo::PsxRuntime invalid{};
    write16(invalid, jojo::PsxBus::timer2_target_address, 0x55aau);
    BIOS_TIMER_REQUIRE(call_b0(invalid, 0x02u, 3u, 0x7777u, 0u) == 0u);
    BIOS_TIMER_REQUIRE(read16(invalid, jojo::PsxBus::timer2_target_address) == 0x55aau);
}

void test_b03_get_timer_reads_current_halfword() {
    jojo::PsxRuntime runtime{};
    constexpr std::array<std::uint16_t, 3> values{{0x1111u, 0x8222u, 0xf333u}};
    for (std::uint32_t timer = 0u; timer < values.size(); ++timer) {
        const auto current = jojo::PsxBus::timer0_current_address + timer * 0x10u;
        write16(runtime, current, values[timer]);
        BIOS_TIMER_REQUIRE(call_b0(runtime, 0x03u, timer) == values[timer]);
    }
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x03u, 3u) == 0u);
}

void test_b04_b05_control_timer_and_vblank_masks() {
    jojo::PsxRuntime runtime{};
    runtime.bus.interrupt_mask = 0u;

    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x04u, 0u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & (1u << 4u)) != 0u);
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x04u, 1u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & (1u << 5u)) != 0u);
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x04u, 2u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & (1u << 6u)) != 0u);
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x04u, 3u) == 0u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & 1u) != 0u);

    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x05u, 0u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & (1u << 4u)) == 0u);
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x05u, 1u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & (1u << 5u)) == 0u);
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x05u, 2u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & (1u << 6u)) == 0u);
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x05u, 3u) == 1u);
    BIOS_TIMER_REQUIRE((runtime.bus.interrupt_mask & 1u) == 0u);
}

void test_b06_restart_timer_clears_current() {
    jojo::PsxRuntime runtime{};
    for (std::uint32_t timer = 0u; timer < 3u; ++timer) {
        const auto current = jojo::PsxBus::timer0_current_address + timer * 0x10u;
        write16(runtime, current, static_cast<std::uint16_t>(0x5000u + timer));
        BIOS_TIMER_REQUIRE(call_b0(runtime, 0x06u, timer) == 1u);
        BIOS_TIMER_REQUIRE(read16(runtime, current) == 0u);
    }
    BIOS_TIMER_REQUIRE(call_b0(runtime, 0x06u, 3u) == 0u);
}

struct BiosTimerContractRunner {
    BiosTimerContractRunner() {
        test_b02_init_timer_programs_root_counters();
        test_b03_get_timer_reads_current_halfword();
        test_b04_b05_control_timer_and_vblank_masks();
        test_b06_restart_timer_clears_current();
    }
};

BiosTimerContractRunner bios_timer_contract_runner{};

} // namespace
