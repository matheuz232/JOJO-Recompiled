#include "core/psx_r3000a.h"
#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

static std::uint32_t encode_r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                              std::uint8_t shamt, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(shamt) << 6u) |
           funct;
}

static std::uint32_t encode_j(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) | (target & 0x03ffffffu);
}

static void test_ori_zero_extends_immediate_and_advances() {
    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c674u);
    cpu.gpr[5] = 0x33330000u;

    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x0d, 5, 5, 0x3333u));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[5] == 0x33333333u);
    CHECK(cpu.pc == 0x8003c678u);
    CHECK(cpu.next_pc == 0x8003c67cu);

    jojo::reset_psx_r3000a(cpu, 0x80001000u);
    cpu.gpr[2] = 0x12340000u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_i(0x0d, 2, 3, 0xffffu)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[3] == 0x1234ffffu);
}

static void test_andi_zero_extends_immediate_and_advances() {
    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c948u);
    cpu.gpr[3] = 0x89abcdefu;

    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x0c, 3, 19, 0xffffu));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[19] == 0x0000cdefu);
    CHECK(cpu.pc == 0x8003c94cu);
    CHECK(cpu.next_pc == 0x8003c950u);

    jojo::reset_psx_r3000a(cpu, 0x80001000u);
    cpu.gpr[2] = 0xffff0000u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_i(0x0c, 2, 3, 0x8001u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[3] == 0u);
}

static void test_sllv_uses_low_five_shift_bits_and_advances() {
    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c950u);
    cpu.gpr[17] = 4u;
    cpu.gpr[3] = 1u;

    const auto result = jojo::step_psx_r3000a(cpu, encode_r(17, 3, 3, 0, 0x04));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[3] == 0x10u);
    CHECK(cpu.pc == 0x8003c954u);
    CHECK(cpu.next_pc == 0x8003c958u);

    jojo::reset_psx_r3000a(cpu, 0x80001000u);
    cpu.gpr[2] = 0x21u;
    cpu.gpr[3] = 3u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_r(2, 3, 4, 0, 0x04)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[4] == 6u);
}

static void test_j_preserves_delay_slot_and_uses_pc_high_nibble() {
    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c964u);
    cpu.gpr[31] = 0x12345678u;

    const auto result = jojo::step_psx_r3000a(cpu, encode_j(0x02u, 0x0000f263u));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[31] == 0x12345678u);
    CHECK(cpu.pc == 0x8003c968u);
    CHECK(cpu.next_pc == 0x8003c98cu);

    CHECK(jojo::step_psx_r3000a(cpu, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003c98cu);

    jojo::reset_psx_r3000a(cpu, 0x9ffffffcu);
    CHECK(jojo::step_psx_r3000a(cpu, encode_j(0x02u, 1u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.next_pc == 0xa0000004u);
}

static void test_gpu_gp0_mmio_port_accepts_command_words() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801810u, 0xe1000400u) ==
          jojo::PsxBusAccessReason::ok);
}

static void test_bios_a0_gpu_cw_returns_after_sending_gp0_command() {
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000a0u);
    runtime.cpu.gpr[4] = 0xe1000400u;
    runtime.cpu.gpr[9] = 0x49u;
    runtime.cpu.gpr[31] = 0x8003ca10u;
    runtime.cpu.gpr[2] = 0xffffffffu;

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.cpu.pc == 0x8003ca10u);
    CHECK(runtime.cpu.next_pc == 0x8003ca14u);
}

int main() {
    test_ori_zero_extends_immediate_and_advances();
    test_andi_zero_extends_immediate_and_advances();
    test_sllv_uses_low_five_shift_bits_and_advances();
    test_j_preserves_delay_slot_and_uses_pc_high_nibble();
    test_gpu_gp0_mmio_port_accepts_command_words();
    test_bios_a0_gpu_cw_returns_after_sending_gp0_command();
    if (failures) return 1;
    std::cout << "R3000A commercial frontier assertions passed\n";
    return 0;
}
