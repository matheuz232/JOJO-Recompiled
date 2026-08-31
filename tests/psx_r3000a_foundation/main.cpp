#include "core/psx_r3000a.h"
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
          jojo::PsxR3000aStepReason::ok); // ADDIU r0,r0,1
    CHECK(state.gpr[0] == 0u);

    state.gpr[1] = 0xffffffffu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(1u, 0u, 0u, 0u, 0x21u)).reason ==
          jojo::PsxR3000aStepReason::ok); // ADDU r0,r1,r0
    CHECK(state.gpr[0] == 0u);
}

static void test_unsigned_add_sub_wrap_exactly_32_bits() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80011000u);
    state.gpr[1] = 0xffffffffu;
    state.gpr[2] = 1u;

    CHECK(jojo::step_psx_r3000a(state, encode_r(1u, 2u, 3u, 0u, 0x21u)).reason ==
          jojo::PsxR3000aStepReason::ok); // ADDU
    CHECK(state.gpr[3] == 0u);

    state.gpr[1] = 0u;
    state.gpr[2] = 1u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(1u, 2u, 4u, 0u, 0x23u)).reason ==
          jojo::PsxR3000aStepReason::ok); // SUBU
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
    const auto result = jojo::step_psx_r3000a(state, 0xfc000000u); // primary opcode 3Fh
    CHECK(result.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(result.exception_code == jojo::PsxR3000aExceptionCode::reserved_instruction);
    CHECK(state.cop0.epc == 0x80014000u);
    CHECK(((state.cop0.cause >> 2u) & 0x1fu) == 10u);
    CHECK(state.pc == 0x80000080u);
}

static void test_gte_register_transfers_match_real_jojo_frontier() {
    jojo::PsxR3000aState state{};

    // COP2 disabled must report the architectural CpU exception and CE=2.
    jojo::reset_psx_r3000a(state, 0x80039c3cu);
    state.gpr[8] = 0x00000155u;
    const auto disabled = jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 29u));
    CHECK(disabled.reason == jojo::PsxR3000aStepReason::exception);
    CHECK(disabled.exception_code == jojo::PsxR3000aExceptionCode::coprocessor_unusable);
    CHECK(((state.cop0.cause >> 28u) & 3u) == 2u);
    CHECK(state.cop0.epc == 0x80039c3cu);

    // Real SLUS_010.60 GTE init sequence: CTC2 ZSF3/ZSF4/H/DQA/DQB/OFX/OFY.
    jojo::reset_psx_r3000a(state, 0x80039c3cu);
    state.cop0.status = 1u << 30u;
    state.gpr[8] = 0x00000155u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 29u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[29] == 0x00000155u);

    state.gpr[8] = 0x00000100u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 30u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[30] == 0x00000100u);

    state.gpr[8] = 0x000003e8u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 26u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[26] == 0x000003e8u);

    state.gpr[8] = 0xffffef9eu;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 27u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[27] == 0xffffef9eu);

    state.gpr[8] = 0x01400000u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 28u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[28] == 0x01400000u);

    state.gpr[0] = 0u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 0u, 24u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 0u, 25u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[24] == 0u);
    CHECK(state.gte.control[25] == 0u);

    // H is unsigned for calculations but the hardware sign-extends it on CFC2.
    state.gpr[8] = 0x0000f000u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x06u, 8u, 26u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.control[26] == 0xfffff000u);

    state.gpr[9] = 0xdeadbeefu;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x02u, 9u, 27u)).reason ==
          jojo::PsxR3000aStepReason::ok); // CFC2 t1,DQA
    CHECK(state.gpr[9] == 0xdeadbeefu);
    CHECK(state.pending_load_valid);
    CHECK(state.pending_load_register == 9u);
    CHECK(state.pending_load_value == 0xffffef9eu);
    CHECK(jojo::step_psx_r3000a(state, encode_r(9u, 0u, 10u, 0u, 0x21u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[10] == 0xdeadbeefu);
    CHECK(state.gpr[9] == 0xffffef9eu);

    // The next real routine writes LZCS and reads the hardware-derived LZCR.
    jojo::reset_psx_r3000a(state, 0x80039c90u);
    state.cop0.status = 1u << 30u;
    state.gpr[4] = 0xf0000000u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x04u, 4u, 30u)).reason ==
          jojo::PsxR3000aStepReason::ok); // MTC2 a0,LZCS
    CHECK(state.gte.data[30] == 0xf0000000u);
    CHECK(state.gte.data[31] == 4u);

    state.gpr[2] = 0xaaaaaaaau;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x00u, 2u, 31u)).reason ==
          jojo::PsxR3000aStepReason::ok); // MFC2 v0,LZCR
    CHECK(state.gpr[2] == 0xaaaaaaaau);
    CHECK(state.pending_load_value == 4u);
    CHECK(jojo::step_psx_r3000a(state, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[2] == 4u);

    // LZCR is read-only.
    state.gpr[4] = 0x12345678u;
    CHECK(jojo::step_psx_r3000a(state, encode_cop2(0x04u, 4u, 31u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gte.data[31] == 4u);
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
