#include "core/sh4_decoder.h"
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

using jojo::Sh4Op;

static void test_fixed_and_integer_instructions() {
    auto i = jojo::decode_sh4(0x0009, 0x8C010000);
    CHECK(i.op == Sh4Op::nop);
    CHECK(i.raw == 0x0009);
    CHECK(i.address == 0x8C010000u);

    i = jojo::decode_sh4(0xE3FB, 0x1000); // MOV #-5,R3
    CHECK(i.op == Sh4Op::mov_imm);
    CHECK(i.rn == 3);
    CHECK(i.immediate == -5);

    i = jojo::decode_sh4(0x72FE, 0x1002); // ADD #-2,R2
    CHECK(i.op == Sh4Op::add_imm);
    CHECK(i.rn == 2);
    CHECK(i.immediate == -2);

    i = jojo::decode_sh4(0x6123, 0x1004); // MOV R2,R1
    CHECK(i.op == Sh4Op::mov_reg);
    CHECK(i.rn == 1);
    CHECK(i.rm == 2);

    i = jojo::decode_sh4(0x312C, 0x1006); // ADD R2,R1
    CHECK(i.op == Sh4Op::add_reg);
    CHECK(i.rn == 1);
    CHECK(i.rm == 2);

    i = jojo::decode_sh4(0x3128, 0x1008); // SUB R2,R1
    CHECK(i.op == Sh4Op::sub_reg);

    i = jojo::decode_sh4(0x3120, 0x100A); // CMP/EQ R2,R1
    CHECK(i.op == Sh4Op::cmp_eq_reg);
}

static void test_control_flow() {
    auto i = jojo::decode_sh4(0xA001, 0x1000); // BRA +2 bytes from PC+4
    CHECK(i.op == Sh4Op::bra);
    CHECK(i.displacement == 2);
    CHECK(i.has_delay_slot);
    CHECK(i.is_branch);
    CHECK(jojo::sh4_direct_target(i).value_or(0) == 0x1006u);

    i = jojo::decode_sh4(0xAFFF, 0x1000); // BRA -2 bytes
    CHECK(i.displacement == -2);
    CHECK(jojo::sh4_direct_target(i).value_or(0) == 0x1002u);

    i = jojo::decode_sh4(0xB002, 0x2000);
    CHECK(i.op == Sh4Op::bsr);
    CHECK(i.has_delay_slot);
    CHECK(i.writes_pr);

    i = jojo::decode_sh4(0x89FE, 0x3000); // BT -4 bytes
    CHECK(i.op == Sh4Op::bt);
    CHECK(i.displacement == -4);
    CHECK(!i.has_delay_slot);
    CHECK(i.conditional);

    i = jojo::decode_sh4(0x8D01, 0x3000); // BT/S +2
    CHECK(i.op == Sh4Op::bt_s);
    CHECK(i.has_delay_slot);
    CHECK(i.conditional);

    i = jojo::decode_sh4(0x452B, 0x4000); // JMP @R5
    CHECK(i.op == Sh4Op::jmp_reg);
    CHECK(i.rn == 5);
    CHECK(i.has_delay_slot);
    CHECK(!jojo::sh4_direct_target(i).has_value());

    i = jojo::decode_sh4(0x460B, 0x4002); // JSR @R6
    CHECK(i.op == Sh4Op::jsr_reg);
    CHECK(i.rn == 6);
    CHECK(i.writes_pr);

    CHECK(jojo::decode_sh4(0x000B, 0x4004).op == Sh4Op::rts);
    CHECK(jojo::decode_sh4(0x002B, 0x4006).op == Sh4Op::rte);
}

static void test_missing_scalar_isa_patterns() {
    auto i = jojo::decode_sh4(0x212C, 0x5000); // CMP/STR R2,R1
    CHECK(i.op == Sh4Op::cmp_str_reg);
    CHECK(i.rn == 1 && i.rm == 2);

    i = jojo::decode_sh4(0x2127, 0x5002); // DIV0S R2,R1
    CHECK(i.op == Sh4Op::div0s);
    CHECK(i.rn == 1 && i.rm == 2);
    CHECK(jojo::decode_sh4(0x0019, 0x5004).op == Sh4Op::div0u);
    CHECK(jojo::decode_sh4(0x3124, 0x5006).op == Sh4Op::div1);

    CHECK(jojo::decode_sh4(0x4120, 0x5008).op == Sh4Op::shal);
    i = jojo::decode_sh4(0x412C, 0x500A); // SHAD R2,R1
    CHECK(i.op == Sh4Op::shad && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x412D, 0x500C); // SHLD R2,R1
    CHECK(i.op == Sh4Op::shld && i.rn == 1 && i.rm == 2);

    CHECK(jojo::decode_sh4(0xCC55, 0x5010).op == Sh4Op::tst_b_imm_gbr);
    CHECK(jojo::decode_sh4(0xCD55, 0x5012).op == Sh4Op::and_b_imm_gbr);
    CHECK(jojo::decode_sh4(0xCE55, 0x5014).op == Sh4Op::xor_b_imm_gbr);
    CHECK(jojo::decode_sh4(0xCF55, 0x5016).op == Sh4Op::or_b_imm_gbr);

    i = jojo::decode_sh4(0x0323, 0x5020); // BRAF R3
    CHECK(i.op == Sh4Op::braf && i.rn == 3 && i.has_delay_slot && i.is_branch);
    i = jojo::decode_sh4(0x0403, 0x5022); // BSRF R4
    CHECK(i.op == Sh4Op::bsrf && i.rn == 4 && i.has_delay_slot && i.writes_pr);
}

static void test_pc_relative_literals() {
    auto i = jojo::decode_sh4(0x9320, 0x1000); // MOV.W @(0x20,PC),R3
    CHECK(i.op == Sh4Op::movw_pc);
    CHECK(i.rn == 3);
    CHECK(i.displacement == 0x40);
    CHECK(jojo::sh4_pc_relative_address(i).value_or(0) == 0x1044u);

    i = jojo::decode_sh4(0xD210, 0x1002); // MOV.L @(0x10,PC),R2
    CHECK(i.op == Sh4Op::movl_pc);
    CHECK(i.rn == 2);
    CHECK(i.displacement == 0x40);
    CHECK(jojo::sh4_pc_relative_address(i).value_or(0) == 0x1044u);

    i = jojo::decode_sh4(0xC703, 0x1006); // MOVA @(3,PC),R0
    CHECK(i.op == Sh4Op::mova_pc);
    CHECK(i.displacement == 12);
    CHECK(jojo::sh4_pc_relative_address(i).value_or(0) == 0x1014u);
}

static void test_stream_and_unsupported() {
    const std::vector<std::uint8_t> bytes = {0x09,0x00, 0xFB,0xE3, 0x0B,0x00};
    const auto stream = jojo::decode_sh4_stream(bytes, 0x8C010000u);
    CHECK(stream);
    if (stream) {
        CHECK(stream.value.size() == 3);
        CHECK(stream.value[0].op == Sh4Op::nop);
        CHECK(stream.value[1].op == Sh4Op::mov_imm);
        CHECK(stream.value[1].address == 0x8C010002u);
        CHECK(stream.value[2].op == Sh4Op::rts);
    }

    const std::vector<std::uint8_t> odd = {0x09,0x00,0xAA};
    CHECK(!jojo::decode_sh4_stream(odd, 0x1000));

    const auto unknown = jojo::decode_sh4(0xFFFF, 0x1000);
    CHECK(unknown.op == Sh4Op::unsupported);
    CHECK(unknown.raw == 0xFFFF);
}

int main() {
    test_fixed_and_integer_instructions();
    test_control_flow();
    test_missing_scalar_isa_patterns();
    test_pc_relative_literals();
    test_stream_and_unsupported();
    if (failures) {
        std::cerr << failures << " SH-4 decoder assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 decoder assertions passed\n";
    return 0;
}
