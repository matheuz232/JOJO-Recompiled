#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) | imm;
}

static std::uint32_t encode_r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                              std::uint8_t shamt, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(shamt) << 6u) | funct;
}

static std::uint32_t encode_j_address(std::uint8_t op, std::uint32_t address) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((address >> 2u) & 0x03ffffffu);
}

static std::uint32_t encode_cop2(std::uint8_t rs, std::uint8_t rt,
                                 std::uint8_t rd) {
    return (0x12u << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u);
}

static jojo::PsxR3000aStepResult step_runtime_instruction(jojo::PsxRuntime& runtime,
                                                          std::uint32_t instruction) {
    CHECK(jojo::psx_bus_write_u32(runtime.bus, runtime.cpu.pc, instruction) ==
          jojo::PsxBusAccessReason::ok);
    return jojo::step_psx_runtime(runtime);
}

static void test_reset_and_zero_register_contract() {
    jojo::PsxR3000aState state{};
    state.gpr.fill(0xffffffffu);
    state.hi = 0xffffffffu;
    state.lo = 0xffffffffu;

    jojo::reset_psx_r3000a(state, 0x8001000cu);
    CHECK(state.pc == 0x8001000cu);
    CHECK(state.next_pc == 0x80010010u);
    for (const auto reg : state.gpr) CHECK(reg == 0u);
    CHECK(state.hi == 0u);
    CHECK(state.lo == 0u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09u, 0u, 0u, 1u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[0] == 0u);

    state.gpr[1] = 0xffffffffu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(1u, 0u, 0u, 0u, 0x21u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[0] == 0u);
}

static void test_unsigned_add_sub_wrap_exactly_32_bits() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80011000u);
    state.gpr[1] = 0xffffffffu;
    state.gpr[2] = 1u;

    CHECK(jojo::step_psx_r3000a(state, encode_r(1u, 2u, 3u, 0u, 0x21u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 0u);

    state.gpr[1] = 0u;
    state.gpr[2] = 1u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(1u, 2u, 4u, 0u, 0x23u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0xffffffffu);
}

static void test_taken_beq_executes_exactly_one_delay_slot() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80012000u);
    state.gpr[1] = 0x12345678u;
    state.gpr[2] = 0x12345678u;

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x04u, 1u, 2u, 3u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x80012004u);
    CHECK(state.next_pc == 0x80012010u);
    CHECK(state.current_instruction_is_branch_delay_slot);
    CHECK(state.branch_pc == 0x80012000u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09u, 0u, 3u, 7u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 7u);
    CHECK(state.pc == 0x80012010u);
    CHECK(state.next_pc == 0x80012014u);
    CHECK(!state.current_instruction_is_branch_delay_slot);
}

static void test_jal_links_pc_plus_eight_and_preserves_delay_slot() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80013000u);

    CHECK(jojo::step_psx_r3000a(state, encode_j_address(0x03u, 0x80040000u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[31] == 0x80013008u);
    CHECK(state.pc == 0x80013004u);
    CHECK(state.next_pc == 0x80040000u);
    CHECK(state.current_instruction_is_branch_delay_slot);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09u, 0u, 5u, 0x55u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[5] == 0x55u);
    CHECK(state.pc == 0x80040000u);
}

static void test_reserved_primary_opcode_is_explicit_exception() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80014000u);
    const auto result = jojo::step_psx_r3000a(state, 0xfc000000u);
    CHECK(result.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(result.exception_code == jojo::PsxR3000aExceptionCode::reserved_instruction);
    CHECK(state.cop0.epc == 0x80014000u);
    CHECK(((state.cop0.cause >> 2u) & 0x1fu) == 10u);
    CHECK(state.pc == 0x80000080u);
}

static void test_gte_register_transfers_match_real_jojo_frontier() {
    jojo::PsxRuntime runtime{};

    jojo::reset_psx_r3000a(runtime.cpu, 0x80039c3cu);
    runtime.cpu.gpr[8] = 0x00000155u;
    const auto disabled = step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 29u));
    CHECK(disabled.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(disabled.exception_code == jojo::PsxR3000aExceptionCode::coprocessor_unusable);
    CHECK(((runtime.cpu.cop0.cause >> 28u) & 3u) == 2u);
    CHECK(runtime.cpu.cop0.epc == 0x80039c3cu);

    runtime = {};
    jojo::reset_psx_r3000a(runtime.cpu, 0x80039c3cu);
    runtime.cpu.cop0.status = 1u << 30u;
    runtime.cpu.gpr[8] = 0x00000155u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 29u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[29] == 0x00000155u);

    runtime.cpu.gpr[8] = 0x00000100u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 30u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[30] == 0x00000100u);

    runtime.cpu.gpr[8] = 0x000003e8u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 26u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[26] == 0x000003e8u);

    runtime.cpu.gpr[8] = 0xffffef9eu;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 27u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[27] == 0xffffef9eu);

    runtime.cpu.gpr[8] = 0x01400000u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 28u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[28] == 0x01400000u);

    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 0u, 24u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 0u, 25u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[24] == 0u);
    CHECK(runtime.gte.control[25] == 0u);

    runtime.cpu.gpr[8] = 0x0000f000u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 26u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.control[26] == 0xfffff000u);

    runtime.cpu.gpr[8] = 0xffffef9eu;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x06u, 8u, 27u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    runtime.cpu.gpr[9] = 0xdeadbeefu;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x02u, 9u, 27u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[9] == 0xdeadbeefu);
    CHECK(runtime.cpu.pending_load_valid);
    CHECK(runtime.cpu.pending_load_register == 9u);
    CHECK(runtime.cpu.pending_load_value == 0xffffef9eu);
    CHECK(step_runtime_instruction(runtime, encode_r(9u, 0u, 10u, 0u, 0x21u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[10] == 0xdeadbeefu);
    CHECK(runtime.cpu.gpr[9] == 0xffffef9eu);

    runtime = {};
    jojo::reset_psx_r3000a(runtime.cpu, 0x80039c90u);
    runtime.cpu.cop0.status = 1u << 30u;
    runtime.cpu.gpr[4] = 0xf0000000u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x04u, 4u, 30u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[30] == 0xf0000000u);
    CHECK(runtime.gte.data[31] == 4u);

    runtime.cpu.gpr[2] = 0xaaaaaaaau;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x00u, 2u, 31u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0xaaaaaaaau);
    CHECK(runtime.cpu.pending_load_value == 4u);
    CHECK(step_runtime_instruction(runtime, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 4u);

    runtime.cpu.gpr[4] = 0x12345678u;
    CHECK(step_runtime_instruction(runtime, encode_cop2(0x04u, 4u, 31u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.gte.data[31] == 4u);
}

int main() {
    test_reset_and_zero_register_contract();
    test_unsigned_add_sub_wrap_exactly_32_bits();
    test_taken_beq_executes_exactly_one_delay_slot();
    test_jal_links_pc_plus_eight_and_preserves_delay_slot();
    test_reserved_primary_opcode_is_explicit_exception();
    test_gte_register_transfers_match_real_jojo_frontier();
    if (failures) return 1;
    std::cout << "R3000A foundation contract assertions passed\n";
    return 0;
}
