#include "core/r3000a_reference_executor.h"
#include "r3000a_test_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

constexpr std::uint32_t ctc2(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x12u << 26) | (0x06u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static jojo::R3000aState base_state() {
    jojo::R3000aState s{};
    s.pc = 0x1000u;
    s.next_pc = 0x1004u;
    return s;
}

static jojo::R3000aState execute_ctc2(std::uint8_t rd, std::uint32_t value) {
    TestR3000aBus bus;
    auto s = base_state();
    s.cop0.status |= 1u << 30;
    s.gpr[8] = value;
    bus.store32(0x1000u, ctc2(8u, rd));
    const auto result = jojo::step_r3000a(s, bus);
    CHECK(result.status == jojo::R3000aStepStatus::retired);
    CHECK(s.pc == 0x1004u);
    return s;
}

static void test_ctc2_respects_cu2_gate() {
    TestR3000aBus bus;
    auto s = base_state();
    s.gpr[8] = 0x155u;
    bus.store32(0x1000u, ctc2(8u, 29u));
    const auto result = jojo::step_r3000a(s, bus);
    CHECK(result.status == jojo::R3000aStepStatus::exception);
    CHECK(result.diagnostic.exception_code == jojo::R3000aExceptionCode::coprocessor_unusable);
    CHECK(result.diagnostic.coprocessor && *result.diagnostic.coprocessor == 2u);
}

static void test_observed_zsf3_ctc2_retires() {
    const auto s = execute_ctc2(29u, 0x00000155u);
    CHECK(s.cop2_gte.control[29] == 0x00000155u);
}

static void test_ctc2_control_write_normalization() {
    CHECK(execute_ctc2(4u,  0xABCD8001u).cop2_gte.control[4]  == 0xFFFF8001u);
    CHECK(execute_ctc2(12u, 0x12347FFFu).cop2_gte.control[12] == 0x00007FFFu);
    CHECK(execute_ctc2(20u, 0x1111FFFFu).cop2_gte.control[20] == 0xFFFFFFFFu);
    CHECK(execute_ctc2(27u, 0x22228000u).cop2_gte.control[27] == 0xFFFF8000u);
    CHECK(execute_ctc2(29u, 0x33338001u).cop2_gte.control[29] == 0xFFFF8001u);
    CHECK(execute_ctc2(30u, 0x44447FFFu).cop2_gte.control[30] == 0x00007FFFu);

    CHECK(execute_ctc2(26u, 0xABCD8001u).cop2_gte.control[26] == 0x00008001u);
    CHECK(execute_ctc2(5u,  0xDEADBEEFu).cop2_gte.control[5]  == 0xDEADBEEFu);
    CHECK(execute_ctc2(0u,  0x12345678u).cop2_gte.control[0]  == 0x12345678u);
}

static void test_ctc2_flag_masks_and_derives_summary_bit() {
    CHECK(execute_ctc2(31u, 0x80000000u).cop2_gte.control[31] == 0u);
    CHECK(execute_ctc2(31u, 0x00002000u).cop2_gte.control[31] == 0x80002000u);
    CHECK(execute_ctc2(31u, 0xFFFFFFFFu).cop2_gte.control[31] == 0xFFFFF000u);
}

int main() {
    test_ctc2_respects_cu2_gate();
    test_observed_zsf3_ctc2_retires();
    test_ctc2_control_write_normalization();
    test_ctc2_flag_masks_and_derives_summary_bit();
    return failures ? 1 : 0;
}
