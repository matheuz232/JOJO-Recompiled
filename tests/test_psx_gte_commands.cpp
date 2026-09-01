#include "core/psx_runtime.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t pack_sxy(std::int16_t x, std::int16_t y) {
    return static_cast<std::uint16_t>(x) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16u);
}

static jojo::PsxR3000aStepResult run_command(jojo::PsxRuntime& runtime,
                                              std::uint32_t instruction) {
    constexpr std::uint32_t pc = 0x80010000u;
    jojo::reset_psx_r3000a(runtime.cpu, pc);
    runtime.cpu.cop0.status |= 1u << 30u;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, instruction) ==
          jojo::PsxBusAccessReason::ok);
    return jojo::step_psx_runtime(runtime);
}

static void test_nclip_executes_real_cop2_command() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[12] = pack_sxy(1, 2);
    runtime.gte.data[13] = pack_sxy(5, 3);
    runtime.gte.data[14] = pack_sxy(4, 8);

    const auto step = run_command(runtime, 0x4a140006u);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    // 1*3 + 5*8 + 4*2 - 1*8 - 5*2 - 4*3 = 21.
    CHECK(runtime.gte.data[24] == 21u);
    CHECK(runtime.cpu.pc == 0x80010004u);
}

static void test_avsz3_updates_mac0_and_otz() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[17] = 0x0100u;
    runtime.gte.data[18] = 0x0200u;
    runtime.gte.data[19] = 0x0300u;
    runtime.gte.control[29] = 0x00001000u;

    const auto step = run_command(runtime, 0x4a15802du);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[24] == 0x00600000u);
    CHECK(runtime.gte.data[7] == 0x0600u);
}

static void test_avsz4_saturates_otz_to_unsigned_16_bit() {
    jojo::PsxRuntime runtime{};
    runtime.gte.data[16] = 0xffffu;
    runtime.gte.data[17] = 0xffffu;
    runtime.gte.data[18] = 0xffffu;
    runtime.gte.data[19] = 0xffffu;
    runtime.gte.control[30] = 0x00007fffu;

    const auto step = run_command(runtime, 0x4a16802eu);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[7] == 0xffffu);
}

int main() {
    test_nclip_executes_real_cop2_command();
    test_avsz3_updates_mac0_and_otz();
    test_avsz4_saturates_otz_to_unsigned_16_bit();
    if (failures) return 1;
    std::cout << "PS1 GTE command assertions passed\n";
    return 0;
}
