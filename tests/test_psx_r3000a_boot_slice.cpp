#include "core/psx_bus.h"
#include "core/psx_r3000a.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                              std::uint8_t shamt, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(shamt) << 6u) |
           funct;
}

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

static void test_lui_and_addiu_build_boot_addresses() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001000cu);
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x0f, 0, 2, 0x8006u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[2] == 0x80060000u);
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 2, 2, 0x36d8u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[2] == 0x800636d8u);
}

static void test_addiu_sign_extends_immediate() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x1000u);
    state.gpr[4] = 0x1000u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 4, 5, 0xfffcu)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[5] == 0x0ffcu);
}

static void test_addi_sign_extends_and_advances() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001009cu);
    state.gpr[4] = 0x00001000u;
    const auto result = jojo::step_psx_r3000a(state, encode_i(0x08, 4, 4, 0xfffcu));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0x00000ffcu);
    CHECK(state.pc == 0x800100a0u);
}

static void test_addi_overflow_stops_without_corrupting_state() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001009cu);
    state.gpr[4] = 0x7fffffffu;
    const auto result = jojo::step_psx_r3000a(state, encode_i(0x08, 4, 4, 1u));
    CHECK(result.reason == jojo::PsxR3000aStepReason::unsupported_instruction);
    CHECK(state.gpr[4] == 0x7fffffffu);
    CHECK(state.pc == 0x8001009cu);
    CHECK(state.next_pc == 0x800100a0u);
}

static void test_sltu_uses_unsigned_comparison() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x2000u);
    state.gpr[2] = 0x80000000u;
    state.gpr[3] = 0xffffffffu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 1, 0, 0x2b)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[1] == 1u);
}

static void test_or_combines_register_bits() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010058u);
    state.gpr[2] = 0x80010000u;
    state.gpr[3] = 0x0000f00fu;
    const auto result = jojo::step_psx_r3000a(state, encode_r(2, 3, 4, 0, 0x25));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0x8001f00fu);
    CHECK(state.pc == 0x8001005cu);
}

static void test_srl_zero_fills_high_bits() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010068u);
    state.gpr[3] = 0x80000003u;
    const auto result = jojo::step_psx_r3000a(state, encode_r(0, 3, 4, 1, 0x02));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0x40000001u);
    CHECK(state.pc == 0x8001006cu);
}

static void test_arithmetic_and_variable_shifts_use_r3000a_rules() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010070u);
    state.gpr[2] = 0x80000003u;

    CHECK(jojo::step_psx_r3000a(state, encode_r(0, 2, 3, 1, 0x03)).reason ==
          jojo::PsxR3000aStepReason::ok); // SRA
    CHECK(state.gpr[3] == 0xc0000001u);

    state.gpr[4] = 0x21u;
    state.gpr[5] = 0x80000001u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(4, 5, 6, 0, 0x06)).reason ==
          jojo::PsxR3000aStepReason::ok); // SRLV
    CHECK(state.gpr[6] == 0x40000000u);

    state.gpr[4] = 4u;
    state.gpr[5] = 0x80000000u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(4, 5, 7, 0, 0x07)).reason ==
          jojo::PsxR3000aStepReason::ok); // SRAV
    CHECK(state.gpr[7] == 0xf8000000u);
}

static void test_register_logical_and_signed_comparison_operations() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010100u);
    state.gpr[2] = 0xf0f000ffu;
    state.gpr[3] = 0x0ff00f0fu;

    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 4, 0, 0x24)).reason ==
          jojo::PsxR3000aStepReason::ok); // AND
    CHECK(state.gpr[4] == 0x00f0000fu);
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 5, 0, 0x26)).reason ==
          jojo::PsxR3000aStepReason::ok); // XOR
    CHECK(state.gpr[5] == 0xff000ff0u);
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 6, 0, 0x27)).reason ==
          jojo::PsxR3000aStepReason::ok); // NOR
    CHECK(state.gpr[6] == 0x000ff000u);

    state.gpr[7] = 0xffffffffu;
    state.gpr[8] = 1u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(7, 8, 9, 0, 0x2a)).reason ==
          jojo::PsxR3000aStepReason::ok); // SLT
    CHECK(state.gpr[9] == 1u);
    CHECK(jojo::step_psx_r3000a(state, encode_r(8, 7, 10, 0, 0x2a)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[10] == 0u);
}

static void test_immediate_logical_and_comparison_operations() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010200u);
    state.gpr[2] = 0xffff0000u;

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x0e, 2, 3, 0x00ffu)).reason ==
          jojo::PsxR3000aStepReason::ok); // XORI
    CHECK(state.gpr[3] == 0xffff00ffu);

    state.gpr[4] = 0xffffffffu;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x0a, 4, 5, 0u)).reason ==
          jojo::PsxR3000aStepReason::ok); // SLTI -1, 0
    CHECK(state.gpr[5] == 1u);

    state.gpr[6] = 0xfffffffeu;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x0b, 6, 7, 0xffffu)).reason ==
          jojo::PsxR3000aStepReason::ok); // SLTIU fffffffe, ffffffff
    CHECK(state.gpr[7] == 1u);
}

static void test_hi_lo_moves_and_multiply_results() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010300u);
    state.gpr[2] = 0x12345678u;
    state.gpr[3] = 0x89abcdefu;

    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 0, 0, 0, 0x11)).reason ==
          jojo::PsxR3000aStepReason::ok); // MTHI
    CHECK(jojo::step_psx_r3000a(state, encode_r(3, 0, 0, 0, 0x13)).reason ==
          jojo::PsxR3000aStepReason::ok); // MTLO
    CHECK(jojo::step_psx_r3000a(state, encode_r(0, 0, 4, 0, 0x10)).reason ==
          jojo::PsxR3000aStepReason::ok); // MFHI
    CHECK(jojo::step_psx_r3000a(state, encode_r(0, 0, 5, 0, 0x12)).reason ==
          jojo::PsxR3000aStepReason::ok); // MFLO
    CHECK(state.gpr[4] == 0x12345678u);
    CHECK(state.gpr[5] == 0x89abcdefu);

    state.gpr[6] = 0xfffffffeu;
    state.gpr[7] = 3u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(6, 7, 0, 0, 0x18)).reason ==
          jojo::PsxR3000aStepReason::ok); // MULT -2 * 3
    CHECK(state.hi == 0xffffffffu);
    CHECK(state.lo == 0xfffffffau);

    state.gpr[6] = 0xffffffffu;
    state.gpr[7] = 2u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(6, 7, 0, 0, 0x19)).reason ==
          jojo::PsxR3000aStepReason::ok); // MULTU
    CHECK(state.hi == 1u);
    CHECK(state.lo == 0xfffffffeu);
}

static void test_division_results_and_psx_edge_behavior() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010400u);

    state.gpr[2] = 0xfffffff9u;
    state.gpr[3] = 3u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 0, 0, 0x1a)).reason ==
          jojo::PsxR3000aStepReason::ok); // DIV -7 / 3
    CHECK(state.lo == 0xfffffffeu);
    CHECK(state.hi == 0xffffffffu);

    state.gpr[2] = 0xfffffffeu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 0, 0, 0x1b)).reason ==
          jojo::PsxR3000aStepReason::ok); // DIVU
    CHECK(state.lo == 0x55555554u);
    CHECK(state.hi == 2u);

    state.gpr[2] = 7u;
    state.gpr[3] = 0u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 0, 0, 0x1a)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.lo == 0xffffffffu);
    CHECK(state.hi == 7u);

    state.gpr[2] = 0xfffffff9u;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 0, 0, 0x1a)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.lo == 1u);
    CHECK(state.hi == 0xfffffff9u);

    state.gpr[2] = 0x89abcdefu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 0, 0, 0x1b)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.lo == 0xffffffffu);
    CHECK(state.hi == 0x89abcdefu);

    state.gpr[2] = 0x80000000u;
    state.gpr[3] = 0xffffffffu;
    CHECK(jojo::step_psx_r3000a(state, encode_r(2, 3, 0, 0, 0x1a)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.lo == 0x80000000u);
    CHECK(state.hi == 0u);
}

static void test_signed_unsigned_subword_loads_and_byte_store() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010500u);
    state.gpr[2] = 0x80001000u;
    state.gpr[3] = 0x11111111u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x8001ff80u) ==
          jojo::PsxBusAccessReason::ok);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x20, 2, 3, 0u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // LB
    CHECK(state.gpr[3] == 0x11111111u);
    CHECK(jojo::step_psx_r3000a(state, 0u, bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 0xffffff80u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x24, 2, 4, 0u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // LBU
    CHECK(jojo::step_psx_r3000a(state, 0u, bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0x00000080u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x21, 2, 5, 2u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // LH
    CHECK(jojo::step_psx_r3000a(state, 0u, bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[5] == 0xffff8001u);

    state.gpr[6] = 0x123456abu;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x28, 2, 6, 1u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // SB
    CHECK(jojo::psx_bus_read_u32(bus, 0x80001000u).value == 0x8001ab80u);

    jojo::reset_psx_r3000a(state, 0x80010600u);
    state.gpr[2] = 0x80001001u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x21, 2, 3, 0u), bus).reason ==
          jojo::PsxR3000aStepReason::memory_fault);
    CHECK(state.pc == 0x80010600u);
}

static void test_scratchpad_storage_and_segment_aliases() {
    jojo::PsxBus bus{};
    CHECK(bus.scratchpad.size() == 1024u);
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f8003fcu, 0x11223344u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x9f8003fcu).value == 0x11223344u);
    CHECK(jojo::psx_bus_read_u32(bus, 0xbf8003fcu).value == 0x11223344u);

    CHECK(jojo::psx_bus_write_u8(bus, 0x1f8003ffu, 0xa5u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u8(bus, 0x9f8003ffu).value == 0xa5u);
    CHECK(jojo::psx_bus_write_u8(bus, 0x1f800400u, 0u) ==
          jojo::PsxBusAccessReason::unmapped);
    CHECK(jojo::psx_bus_read_u8(bus, 0x1f800400u).reason ==
          jojo::PsxBusAccessReason::unmapped);
}

static void test_lwl_lwr_pair_merges_pending_load_for_little_endian_word() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010700u);
    state.gpr[2] = 0x80001001u;
    state.gpr[3] = 0xaabbccddu;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x33221100u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001004u, 0x77665544u) ==
          jojo::PsxBusAccessReason::ok);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x22, 2, 3, 3u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // LWL at address + 3
    CHECK(state.gpr[3] == 0xaabbccddu);
    CHECK(state.pending_load_value == 0x44bbccddu);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x26, 2, 3, 0u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // LWR merges forwarded pending value
    CHECK(state.gpr[3] == 0xaabbccddu);
    CHECK(state.pending_load_value == 0x44332211u);

    CHECK(jojo::step_psx_r3000a(state, 0u, bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 0x44332211u);
}

static void test_swl_swr_pair_stores_little_endian_unaligned_word() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010800u);
    state.gpr[2] = 0x80001001u;
    state.gpr[3] = 0x44332211u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0xaaaaaaaau) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001004u, 0xbbbbbbbbu) ==
          jojo::PsxBusAccessReason::ok);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x2a, 2, 3, 3u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // SWL
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x2e, 2, 3, 0u), bus).reason ==
          jojo::PsxR3000aStepReason::ok); // SWR
    CHECK(jojo::psx_bus_read_u32(bus, 0x80001000u).value == 0x332211aau);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80001004u).value == 0xbbbbbb44u);

    jojo::reset_psx_r3000a(state, 0x80010900u);
    state.gpr[2] = 0x1f801001u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x2e, 2, 3, 0u), bus).reason ==
          jojo::PsxR3000aStepReason::memory_fault);
    CHECK(state.pc == 0x80010900u);
}

static void test_unaligned_word_merge_masks_for_every_byte_offset() {
    constexpr std::uint32_t lwl_expected[] = {
        0x11bbccddu, 0x2211ccddu, 0x332211ddu, 0x44332211u,
    };
    constexpr std::uint32_t lwr_expected[] = {
        0x44332211u, 0xaa443322u, 0xaabb4433u, 0xaabbcc44u,
    };
    constexpr std::uint32_t swl_expected[] = {
        0x443322aau, 0x4433aabbu, 0x44aabbccu, 0xaabbccddu,
    };
    constexpr std::uint32_t swr_expected[] = {
        0xaabbccddu, 0xbbccdd11u, 0xccdd2211u, 0xdd332211u,
    };

    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    for (std::uint32_t offset = 0u; offset < 4u; ++offset) {
        CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x44332211u) ==
              jojo::PsxBusAccessReason::ok);
        jojo::reset_psx_r3000a(state, 0x80010a00u);
        state.gpr[2] = 0x80001000u + offset;
        state.gpr[3] = 0xaabbccddu;
        CHECK(jojo::step_psx_r3000a(state, encode_i(0x22, 2, 3, 0u), bus).reason ==
              jojo::PsxR3000aStepReason::ok);
        CHECK(jojo::step_psx_r3000a(state, 0u, bus).reason ==
              jojo::PsxR3000aStepReason::ok);
        CHECK(state.gpr[3] == lwl_expected[offset]);

        jojo::reset_psx_r3000a(state, 0x80010b00u);
        state.gpr[2] = 0x80001000u + offset;
        state.gpr[3] = 0xaabbccddu;
        CHECK(jojo::step_psx_r3000a(state, encode_i(0x26, 2, 3, 0u), bus).reason ==
              jojo::PsxR3000aStepReason::ok);
        CHECK(jojo::step_psx_r3000a(state, 0u, bus).reason ==
              jojo::PsxR3000aStepReason::ok);
        CHECK(state.gpr[3] == lwr_expected[offset]);

        CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x44332211u) ==
              jojo::PsxBusAccessReason::ok);
        jojo::reset_psx_r3000a(state, 0x80010c00u);
        state.gpr[2] = 0x80001000u + offset;
        state.gpr[3] = 0xaabbccddu;
        CHECK(jojo::step_psx_r3000a(state, encode_i(0x2a, 2, 3, 0u), bus).reason ==
              jojo::PsxR3000aStepReason::ok);
        CHECK(jojo::psx_bus_read_u32(bus, 0x80001000u).value == swl_expected[offset]);

        CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x44332211u) ==
              jojo::PsxBusAccessReason::ok);
        jojo::reset_psx_r3000a(state, 0x80010d00u);
        state.gpr[2] = 0x80001000u + offset;
        state.gpr[3] = 0xaabbccddu;
        CHECK(jojo::step_psx_r3000a(state, encode_i(0x2e, 2, 3, 0u), bus).reason ==
              jojo::PsxR3000aStepReason::ok);
        CHECK(jojo::psx_bus_read_u32(bus, 0x80001000u).value == swr_expected[offset]);
    }
}

static void test_jr_preserves_delay_slot_then_uses_register_target() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80035a34u);
    state.gpr[10] = 0x000000a0u;

    CHECK(jojo::step_psx_r3000a(state, encode_r(10, 0, 0, 0, 0x08)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x80035a38u);
    CHECK(state.next_pc == 0x000000a0u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 0, 9, 0x0039u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[9] == 0x39u);
    CHECK(state.pc == 0x000000a0u);
}

static void test_jalr_links_and_preserves_delay_slot() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8003c4b8u);
    state.gpr[10] = 0x80042000u;

    const auto result = jojo::step_psx_r3000a(state, encode_r(10, 0, 31, 0, 0x09));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[31] == 0x8003c4c0u);
    CHECK(state.pc == 0x8003c4bcu);
    CHECK(state.next_pc == 0x80042000u);

    CHECK(jojo::step_psx_r3000a(state, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x80042000u);
}

static void test_jalr_reads_target_before_writing_same_register_link() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x00004000u);
    state.gpr[5] = 0x80022220u;

    const auto result = jojo::step_psx_r3000a(state, encode_r(5, 0, 5, 0, 0x09));
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[5] == 0x00004008u);
    CHECK(state.pc == 0x00004004u);
    CHECK(state.next_pc == 0x80022220u);
}

static void test_bne_taken_preserves_delay_slot() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x3000u);
    state.gpr[1] = 1u;
    state.gpr[2] = 0u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x05, 1, 2, 0xfffcu)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x3004u);
    CHECK(state.next_pc == 0x2ff4u);
    CHECK(jojo::step_psx_r3000a(state, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.pc == 0x2ff4u);
}

static void test_zero_comparison_branches_preserve_delay_slots() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x00004000u);
    state.gpr[2] = 0xffffffffu;

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x06, 2, 0, 2u)).reason ==
          jojo::PsxR3000aStepReason::ok); // BLEZ taken
    CHECK(state.pc == 0x00004004u);
    CHECK(state.next_pc == 0x0000400cu);
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 0, 3, 7u)).reason ==
          jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 7u);
    CHECK(state.pc == 0x0000400cu);

    jojo::reset_psx_r3000a(state, 0x00005000u);
    state.gpr[2] = 1u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x07, 2, 0, 0xfffeu)).reason ==
          jojo::PsxR3000aStepReason::ok); // BGTZ taken
    CHECK(state.pc == 0x00005004u);
    CHECK(state.next_pc == 0x00004ffcu);

    jojo::reset_psx_r3000a(state, 0x00006000u);
    state.gpr[2] = 1u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x06, 2, 0, 1u)).reason ==
          jojo::PsxR3000aStepReason::ok); // BLEZ not taken
    CHECK(state.pc == 0x00006004u);
    CHECK(state.next_pc == 0x00006008u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x06, 2, 1, 0u)).reason ==
          jojo::PsxR3000aStepReason::unsupported_instruction);
}

static void test_regimm_branches_link_unconditionally_and_preserve_delay_slots() {
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x00007000u);
    state.gpr[2] = 0x80000000u;

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x01, 2, 0x00, 3u)).reason ==
          jojo::PsxR3000aStepReason::ok); // BLTZ taken
    CHECK(state.pc == 0x00007004u);
    CHECK(state.next_pc == 0x00007010u);

    jojo::reset_psx_r3000a(state, 0x00008000u);
    state.gpr[2] = 0u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x01, 2, 0x01, 2u)).reason ==
          jojo::PsxR3000aStepReason::ok); // BGEZ taken
    CHECK(state.next_pc == 0x0000800cu);

    jojo::reset_psx_r3000a(state, 0x00009000u);
    state.gpr[2] = 1u;
    state.gpr[31] = 0xdeadbeefu;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x01, 2, 0x10, 2u)).reason ==
          jojo::PsxR3000aStepReason::ok); // BLTZAL not taken, link still written
    CHECK(state.gpr[31] == 0x00009008u);
    CHECK(state.pc == 0x00009004u);
    CHECK(state.next_pc == 0x00009008u);

    jojo::reset_psx_r3000a(state, 0x0000a000u);
    state.gpr[2] = 0u;
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x01, 2, 0x11, 0xfffdu)).reason ==
          jojo::PsxR3000aStepReason::ok); // BGEZAL taken
    CHECK(state.gpr[31] == 0x0000a008u);
    CHECK(state.pc == 0x0000a004u);
    CHECK(state.next_pc == 0x00009ff8u);

    CHECK(jojo::step_psx_r3000a(state, encode_i(0x01, 2, 0x02, 0u)).reason ==
          jojo::PsxR3000aStepReason::unsupported_instruction);
}

static void test_main_ram_is_two_megabytes_and_zero_initialized() {
    jojo::PsxBus bus{};
    CHECK(bus.ram.size() == 2u * 1024u * 1024u);
    const auto first = jojo::psx_bus_read_u32(bus, 0x00000000u);
    CHECK(first.reason == jojo::PsxBusAccessReason::ok);
    CHECK(first.value == 0u);
}

static void test_ram_kuseg_kseg0_kseg1_and_default_8mb_window_alias_storage() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x00000100u, 0x11223344u) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80000100u).value == 0x11223344u);
    CHECK(jojo::psx_bus_read_u32(bus, 0xa0000100u).value == 0x11223344u);
    CHECK(jojo::psx_bus_write_u32(bus, 0x80200104u, 0xaabbccddu) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_read_u32(bus, 0x00000104u).value == 0xaabbccddu);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80600104u).value == 0xaabbccddu);
}

static void test_ram_access_rejects_unaligned_and_unmapped_addresses() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x80000002u, 1u) == jojo::PsxBusAccessReason::misaligned);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80000002u).reason == jojo::PsxBusAccessReason::misaligned);
    CHECK(jojo::psx_bus_write_u32(bus, 0x80800000u, 1u) == jojo::PsxBusAccessReason::unmapped);
    CHECK(jojo::psx_bus_read_u32(bus, 0x1f801000u).reason == jojo::PsxBusAccessReason::unmapped);
}

static void test_sw_uses_signed_offset_and_writes_through_bus() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001000cu);
    state.gpr[2] = 0x800636d8u;
    state.gpr[3] = 0xdeadbeefu;
    const auto result = jojo::step_psx_r3000a(state, encode_i(0x2b, 2, 3, 0xfffcu), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    const auto stored = jojo::psx_bus_read_u32(bus, 0x800636d4u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0xdeadbeefu);
    CHECK(state.pc == 0x80010010u);
}

static void test_sw_reports_memory_fault_without_advancing_pipeline() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8001000cu);
    state.gpr[2] = 0x1f801000u;
    state.gpr[3] = 0x12345678u;
    const auto result = jojo::step_psx_r3000a(state, encode_i(0x2b, 2, 3, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::memory_fault);
    CHECK(state.pc == 0x8001000cu);
    CHECK(state.next_pc == 0x80010010u);
}

static void test_sh_uses_signed_offset_truncates_and_writes_halfword() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8003c66cu);
    state.gpr[2] = 0x80001004u;
    state.gpr[3] = 0x123489abu;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0xdeadbeefu) == jojo::PsxBusAccessReason::ok);

    const auto result = jojo::step_psx_r3000a(state, encode_i(0x29, 2, 3, 0xfffeu), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    const auto stored = jojo::psx_bus_read_u32(bus, 0x80001000u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0x89abbeefu);
    CHECK(state.pc == 0x8003c670u);
}

static void test_sh_rejects_odd_address_without_advancing_pipeline() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8003c66cu);
    state.gpr[2] = 0x80001001u;
    state.gpr[3] = 0x00001234u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0xaabbccddu) == jojo::PsxBusAccessReason::ok);

    const auto result = jojo::step_psx_r3000a(state, encode_i(0x29, 2, 3, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::memory_fault);
    CHECK(jojo::psx_bus_read_u32(bus, 0x80001000u).value == 0xaabbccddu);
    CHECK(state.pc == 0x8003c66cu);
    CHECK(state.next_pc == 0x8003c670u);
}

static void test_lhu_reads_unsigned_halfword_with_load_delay() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8003c648u);
    state.gpr[2] = 0x80001004u;
    state.gpr[3] = 0x11111111u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x89abcdefu) == jojo::PsxBusAccessReason::ok);

    const auto result = jojo::step_psx_r3000a(state, encode_i(0x25, 2, 3, 0xfffeu), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 0x11111111u);
    CHECK(state.pc == 0x8003c64cu);

    CHECK(jojo::step_psx_r3000a(state, encode_r(3, 0, 4, 0, 0x21), bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0x11111111u);
    CHECK(state.gpr[3] == 0x000089abu);
}

static void test_lhu_rejects_odd_address_without_advancing_pipeline() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x8003c648u);
    state.gpr[2] = 0x80001001u;
    state.gpr[3] = 0x12345678u;

    const auto result = jojo::step_psx_r3000a(state, encode_i(0x25, 2, 3, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::memory_fault);
    CHECK(state.gpr[3] == 0x12345678u);
    CHECK(state.pc == 0x8003c648u);
    CHECK(state.next_pc == 0x8003c64cu);
}

static void test_lw_uses_signed_offset_and_defers_register_update() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010050u);
    state.gpr[2] = 0x80001004u;
    state.gpr[3] = 0x11111111u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x89abcdefu) == jojo::PsxBusAccessReason::ok);
    const auto result = jojo::step_psx_r3000a(state, encode_i(0x23, 2, 3, 0xfffcu), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 0x11111111u);
    CHECK(state.pc == 0x80010054u);
}

static void test_lw_delay_slot_reads_old_value_then_loaded_value_becomes_visible() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010050u);
    state.gpr[2] = 0x80001000u;
    state.gpr[3] = 0x11111111u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x89abcdefu) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x23, 2, 3, 0u), bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::step_psx_r3000a(state, encode_r(3, 0, 4, 0, 0x21), bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[4] == 0x11111111u);
    CHECK(state.gpr[3] == 0x89abcdefu);
}

static void test_load_delay_slot_write_to_same_register_wins() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010050u);
    state.gpr[2] = 0x80001000u;
    state.gpr[3] = 0x11111111u;
    CHECK(jojo::psx_bus_write_u32(bus, 0x80001000u, 0x89abcdefu) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x23, 2, 3, 0u), bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(jojo::step_psx_r3000a(state, encode_i(0x09, 0, 3, 7u), bus).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(state.gpr[3] == 7u);
}

static void test_lw_memory_fault_does_not_advance_or_modify_target() {
    jojo::PsxBus bus{};
    jojo::PsxR3000aState state{};
    jojo::reset_psx_r3000a(state, 0x80010050u);
    state.gpr[2] = 0x80001002u;
    state.gpr[3] = 0x12345678u;
    const auto result = jojo::step_psx_r3000a(state, encode_i(0x23, 2, 3, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::memory_fault);
    CHECK(state.gpr[3] == 0x12345678u);
    CHECK(state.pc == 0x80010050u);
    CHECK(state.next_pc == 0x80010054u);
}

int main() {
    test_lui_and_addiu_build_boot_addresses();
    test_addiu_sign_extends_immediate();
    test_addi_sign_extends_and_advances();
    test_addi_overflow_stops_without_corrupting_state();
    test_sltu_uses_unsigned_comparison();
    test_or_combines_register_bits();
    test_srl_zero_fills_high_bits();
    test_arithmetic_and_variable_shifts_use_r3000a_rules();
    test_register_logical_and_signed_comparison_operations();
    test_immediate_logical_and_comparison_operations();
    test_hi_lo_moves_and_multiply_results();
    test_division_results_and_psx_edge_behavior();
    test_signed_unsigned_subword_loads_and_byte_store();
    test_scratchpad_storage_and_segment_aliases();
    test_lwl_lwr_pair_merges_pending_load_for_little_endian_word();
    test_swl_swr_pair_stores_little_endian_unaligned_word();
    test_unaligned_word_merge_masks_for_every_byte_offset();
    test_jr_preserves_delay_slot_then_uses_register_target();
    test_jalr_links_and_preserves_delay_slot();
    test_jalr_reads_target_before_writing_same_register_link();
    test_bne_taken_preserves_delay_slot();
    test_zero_comparison_branches_preserve_delay_slots();
    test_regimm_branches_link_unconditionally_and_preserve_delay_slots();
    test_main_ram_is_two_megabytes_and_zero_initialized();
    test_ram_kuseg_kseg0_kseg1_and_default_8mb_window_alias_storage();
    test_ram_access_rejects_unaligned_and_unmapped_addresses();
    test_sw_uses_signed_offset_and_writes_through_bus();
    test_sw_reports_memory_fault_without_advancing_pipeline();
    test_sh_uses_signed_offset_truncates_and_writes_halfword();
    test_sh_rejects_odd_address_without_advancing_pipeline();
    test_lhu_reads_unsigned_halfword_with_load_delay();
    test_lhu_rejects_odd_address_without_advancing_pipeline();
    test_lw_uses_signed_offset_and_defers_register_update();
    test_lw_delay_slot_reads_old_value_then_loaded_value_becomes_visible();
    test_load_delay_slot_write_to_same_register_wins();
    test_lw_memory_fault_does_not_advance_or_modify_target();
    if (failures) return 1;
    std::cout << "R3000A boot integer and RAM load/store assertions passed\n";
    return 0;
}
