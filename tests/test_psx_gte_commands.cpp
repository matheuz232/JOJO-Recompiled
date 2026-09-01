#include "core/psx_runtime.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t pack_sxy(std::int16_t x, std::int16_t y) {
    return static_cast<std::uint16_t>(x) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16u);
}

static void prepare_runtime(jojo::PsxRuntime& runtime,
                            std::uint32_t pc = 0x80010000u) {
    jojo::reset_psx_r3000a(runtime.cpu, pc);
    runtime.cpu.cop0.status |= 1u << 30u;
}

static jojo::PsxR3000aStepResult run_command(jojo::PsxRuntime& runtime,
                                              std::uint32_t instruction) {
    constexpr std::uint32_t pc = 0x80010000u;
    prepare_runtime(runtime, pc);
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

static void test_lwc2_loads_ram_word_into_gte_data_register() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, base + 4u, 0x12345678u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xc8220004u) ==
          jojo::PsxBusAccessReason::ok); // LWC2 r2,4(r1)

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[2] == 0x12345678u);
    CHECK(runtime.cpu.pc == pc + 4u);
}

static void test_swc2_stores_gte_data_register_into_ram() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    jojo::psx_gte_write_data(runtime.gte, 2u, 0x89abcdefu);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xe8220008u) ==
          jojo::PsxBusAccessReason::ok); // SWC2 r2,8(r1)

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::ok);
    const auto stored = jojo::psx_bus_read_u32(runtime.bus, base + 8u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0x89abcdefu);
}

static void test_lwc2_requires_enabled_cop2() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    jojo::reset_psx_r3000a(runtime.cpu, pc);
    runtime.cpu.gpr[1] = 0x80010100u;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xc8220000u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(step.exception_code == jojo::PsxR3000aExceptionCode::coprocessor_unusable);
    CHECK(((runtime.cpu.cop0.cause >> 28u) & 3u) == 2u);
}

static void test_lwc2_misalignment_raises_address_error_load() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xc8220002u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(step.exception_code == jojo::PsxR3000aExceptionCode::address_error_load);
    CHECK(runtime.cpu.cop0.bad_vaddr == base + 2u);
}

static void test_swc2_misalignment_raises_address_error_store() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t pc = 0x80010000u;
    constexpr std::uint32_t base = 0x80010100u;
    prepare_runtime(runtime, pc);
    runtime.cpu.gpr[1] = base;
    CHECK(jojo::psx_bus_write_u32(runtime.bus, pc, 0xe8220002u) ==
          jojo::PsxBusAccessReason::ok);

    const auto step = jojo::step_psx_runtime(runtime);
    CHECK(step.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(step.exception_code == jojo::PsxR3000aExceptionCode::address_error_store);
    CHECK(runtime.cpu.cop0.bad_vaddr == base + 2u);
}

int main() {
    test_nclip_executes_real_cop2_command();
    test_avsz3_updates_mac0_and_otz();
    test_avsz4_saturates_otz_to_unsigned_16_bit();
    test_lwc2_loads_ram_word_into_gte_data_register();
    test_swc2_stores_gte_data_register_into_ram();
    test_lwc2_requires_enabled_cop2();
    test_lwc2_misalignment_raises_address_error_load();
    test_swc2_misalignment_raises_address_error_store();
    if (failures) return 1;
    std::cout << "PS1 GTE command assertions passed\n";
    return 0;
}
