#include "core/sh4_ir.h"
#include "core/sh4_cfg.h"
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static void test_lifts_integer_register_ops() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xE3FB);
    append_word(bytes, 0x7302);
    append_word(bytes, 0x6433);
    append_word(bytes, 0x000B);
    append_word(bytes, 0x0009);
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x1000, 0x1000);
    CHECK(cfg); if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return;
    const auto& ops = ir.value.blocks[0].ops;
    CHECK(ops.size() == 5);
    CHECK(ops[0].op == jojo::Sh4IrOp::set_imm && ops[0].dst_reg == 3 && ops[0].imm == -5);
    CHECK(ops[1].op == jojo::Sh4IrOp::add_imm && ops[1].dst_reg == 3 && ops[1].imm == 2);
    CHECK(ops[2].op == jojo::Sh4IrOp::copy_reg && ops[2].dst_reg == 4 && ops[2].src_reg == 3);
    CHECK(ops[3].op == jojo::Sh4IrOp::return_pr && !ops[3].in_delay_slot);
    CHECK(ops[4].op == jojo::Sh4IrOp::nop && ops[4].in_delay_slot);
}

static void test_lifts_data_memory_modes() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0x2120); // MOV.B R2,@R1
    append_word(bytes, 0x6341); // MOV.W @R4,R3
    append_word(bytes, 0x2566); // MOV.L R6,@-R5
    append_word(bytes, 0x6784); // MOV.B @R8+,R7
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x1800, 0x1800);
    CHECK(cfg); if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return;
    const auto& ops = ir.value.blocks[0].ops;
    CHECK(ops.size() == 4);
    CHECK(ops[0].op == jojo::Sh4IrOp::store_memory);
    CHECK(ops[0].memory_width == jojo::Sh4IrMemoryWidth::byte);
    CHECK(ops[0].addressing == jojo::Sh4IrAddressing::indirect);
    CHECK(ops[0].dst_reg == 1 && ops[0].src_reg == 2);

    CHECK(ops[1].op == jojo::Sh4IrOp::load_memory);
    CHECK(ops[1].memory_width == jojo::Sh4IrMemoryWidth::word);
    CHECK(ops[1].addressing == jojo::Sh4IrAddressing::indirect);
    CHECK(ops[1].dst_reg == 3 && ops[1].src_reg == 4);

    CHECK(ops[2].op == jojo::Sh4IrOp::store_memory);
    CHECK(ops[2].memory_width == jojo::Sh4IrMemoryWidth::long_word);
    CHECK(ops[2].addressing == jojo::Sh4IrAddressing::pre_decrement);
    CHECK(ops[2].dst_reg == 5 && ops[2].src_reg == 6);

    CHECK(ops[3].op == jojo::Sh4IrOp::load_memory);
    CHECK(ops[3].memory_width == jojo::Sh4IrMemoryWidth::byte);
    CHECK(ops[3].addressing == jojo::Sh4IrAddressing::post_increment);
    CHECK(ops[3].dst_reg == 7 && ops[3].src_reg == 8);
}

static void test_preserves_delayed_branch_order() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xA001);
    append_word(bytes, 0x7101);
    append_word(bytes, 0x0009);
    append_word(bytes, 0x000B);
    append_word(bytes, 0x0009);
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x2000, 0x2000);
    CHECK(cfg); if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return;
    const auto* entry = jojo::find_sh4_ir_block(ir.value, 0x2000);
    CHECK(entry != nullptr); if (!entry) return;
    CHECK(entry->ops.size() == 2);
    CHECK(entry->ops[0].op == jojo::Sh4IrOp::branch_direct && entry->ops[0].target == 0x2006u);
    CHECK(!entry->ops[0].in_delay_slot);
    CHECK(entry->ops[1].op == jojo::Sh4IrOp::add_imm && entry->ops[1].dst_reg == 1);
    CHECK(entry->ops[1].in_delay_slot);
    CHECK(entry->exit == jojo::Sh4IrExit::direct_branch);
    CHECK(entry->branch_target.value_or(0) == 0x2006u);
}

static void test_lifts_condition_and_calls_without_executing() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0x3120);
    append_word(bytes, 0x8901);
    append_word(bytes, 0xB002);
    append_word(bytes, 0x0009);
    append_word(bytes, 0x000B);
    append_word(bytes, 0x0009);
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x3000, 0x3000);
    CHECK(cfg); if (!cfg) return;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir); if (!ir) return;
    const auto* entry = jojo::find_sh4_ir_block(ir.value, 0x3000);
    CHECK(entry != nullptr);
    if (entry) {
        CHECK(entry->ops[0].op == jojo::Sh4IrOp::compare_eq && entry->ops[0].dst_reg == 1 && entry->ops[0].src_reg == 2);
        CHECK(entry->ops[1].op == jojo::Sh4IrOp::branch_if_t && entry->ops[1].target == 0x3008u);
        CHECK(entry->fallthrough_target.value_or(0) == 0x3004u);
    }
    const auto* call = jojo::find_sh4_ir_block(ir.value, 0x3004);
    CHECK(call != nullptr);
    if (call) {
        CHECK(call->ops[0].op == jojo::Sh4IrOp::call_direct && call->ops[0].target == 0x300Cu);
        CHECK(call->ops[1].in_delay_slot);
        CHECK(call->fallthrough_target.value_or(0) == 0x3008u);
    }
}

int main() {
    test_lifts_integer_register_ops();
    test_lifts_data_memory_modes();
    test_preserves_delayed_branch_order();
    test_lifts_condition_and_calls_without_executing();
    if (failures) { std::cerr << failures << " SH-4 IR assertion(s) failed\n"; return 1; }
    std::cout << "all SH-4 IR assertions passed\n";
    return 0;
}
