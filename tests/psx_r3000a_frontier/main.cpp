#include "core/psx_r3000a.h"
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

int main() {
    test_ori_zero_extends_immediate_and_advances();
    test_andi_zero_extends_immediate_and_advances();
    if (failures) return 1;
    std::cout << "R3000A commercial frontier assertions passed\n";
    return 0;
}
