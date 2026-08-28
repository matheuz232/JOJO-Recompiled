#include "core/sh4_reference_executor.h"
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::Sh4IrInstruction op(jojo::Sh4IrOp kind,
                                 std::uint32_t address,
                                 std::uint8_t dst = 0xFF,
                                 std::uint8_t src = 0xFF,
                                 std::int32_t imm = 0,
                                 std::uint32_t target = 0,
                                 bool delay = false) {
    jojo::Sh4IrInstruction out{};
    out.op = kind;
    out.source_address = address;
    out.dst_reg = dst;
    out.src_reg = src;
    out.imm = imm;
    out.target = target;
    out.in_delay_slot = delay;
    return out;
}

static jojo::Sh4IrInstruction memop(jojo::Sh4IrOp kind,
                                    std::uint32_t address,
                                    std::uint8_t dst,
                                    std::uint8_t src,
                                    jojo::Sh4IrMemoryWidth width,
                                    jojo::Sh4IrAddressing addressing) {
    auto out = op(kind, address, dst, src);
    out.memory_width = width;
    out.addressing = addressing;
    return out;
}

static jojo::Sh4IrProgram one_block(std::uint32_t address,
                                    std::vector<jojo::Sh4IrInstruction> ops,
                                    jojo::Sh4IrExit exit = jojo::Sh4IrExit::end_of_stream) {
    jojo::Sh4IrBlock block{};
    block.start_address = address;
    block.ops = std::move(ops);
    block.exit = exit;
    jojo::Sh4IrProgram program{};
    program.entry_address = address;
    program.blocks.push_back(std::move(block));
    return program;
}

static void test_integer_state_and_compare() {
    auto program = one_block(0x1000, {
        op(jojo::Sh4IrOp::set_imm, 0x1000, 1, 0xFF, 5),
        op(jojo::Sh4IrOp::set_imm, 0x1002, 2, 0xFF, 5),
        op(jojo::Sh4IrOp::compare_eq, 0x1004, 1, 2),
        op(jojo::Sh4IrOp::add_imm, 0x1006, 1, 0xFF, -2),
    });
    jojo::Sh4ReferenceState state{};
    const jojo::Sh4ReferenceMemoryView memory{};
    const auto run = jojo::execute_sh4_ir_reference(program, state, memory, 8);
    CHECK(run);
    if (!run) return;
    CHECK(state.r[1] == 3u);
    CHECK(state.r[2] == 5u);
    CHECK(state.t);
    CHECK(state.pc == 0x1008u);
    CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::end_of_stream);
    CHECK(run.value.blocks_executed == 1u);
    CHECK(run.value.operations_executed == 4u);
}

static void test_data_memory_load_store_and_address_updates() {
    std::vector<std::uint8_t> bytes(16, 0);
    auto program = one_block(0x1400, {
        memop(jojo::Sh4IrOp::store_memory, 0x1400, 1, 2,
              jojo::Sh4IrMemoryWidth::byte, jojo::Sh4IrAddressing::indirect),
        memop(jojo::Sh4IrOp::load_memory, 0x1402, 3, 1,
              jojo::Sh4IrMemoryWidth::byte, jojo::Sh4IrAddressing::indirect),
        memop(jojo::Sh4IrOp::store_memory, 0x1404, 4, 5,
              jojo::Sh4IrMemoryWidth::word, jojo::Sh4IrAddressing::pre_decrement),
        memop(jojo::Sh4IrOp::load_memory, 0x1406, 6, 4,
              jojo::Sh4IrMemoryWidth::word, jojo::Sh4IrAddressing::post_increment),
        memop(jojo::Sh4IrOp::store_memory, 0x1408, 7, 8,
              jojo::Sh4IrMemoryWidth::long_word, jojo::Sh4IrAddressing::indirect),
        memop(jojo::Sh4IrOp::load_memory, 0x140A, 9, 7,
              jojo::Sh4IrMemoryWidth::long_word, jojo::Sh4IrAddressing::indirect),
    });

    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x7001;
    state.r[2] = 0x12345680;
    state.r[4] = 0x7008;
    state.r[5] = 0xA1B2;
    state.r[7] = 0x7008;
    state.r[8] = 0x89ABCDEF;
    jojo::Sh4ReferenceMemoryView memory{0x7000, bytes};

    const auto run = jojo::execute_sh4_ir_reference(program, state, memory, 8);
    CHECK(run);
    if (!run) return;

    CHECK(bytes[1] == 0x80u);
    CHECK(state.r[3] == 0xFFFFFF80u);

    CHECK(state.r[4] == 0x7008u);
    CHECK(bytes[6] == 0xB2u && bytes[7] == 0xA1u);
    CHECK(state.r[6] == 0xFFFFA1B2u);

    CHECK(bytes[8] == 0xEFu && bytes[9] == 0xCDu && bytes[10] == 0xABu && bytes[11] == 0x89u);
    CHECK(state.r[9] == 0x89ABCDEFu);

    program.blocks[0].ops[1].src_reg = 10;
    state.r[10] = 0x8000;
    const auto bad = jojo::execute_sh4_ir_reference(program, state, memory, 8);
    CHECK(!bad);
    if (!bad) CHECK(bad.error == jojo::ErrorCode::invalid_argument);
}

static void test_delayed_conditional_branch_latches_decision() {
    jojo::Sh4IrBlock entry{};
    entry.start_address = 0x2000;
    entry.exit = jojo::Sh4IrExit::conditional_branch;
    entry.branch_target = 0x2010;
    entry.fallthrough_target = 0x2006;
    entry.ops = {
        op(jojo::Sh4IrOp::compare_eq, 0x2000, 1, 2),
        op(jojo::Sh4IrOp::branch_if_t, 0x2002, 0xFF, 0xFF, 0, 0x2010),
        op(jojo::Sh4IrOp::compare_eq, 0x2004, 1, 3, 0, 0, true),
    };
    jojo::Sh4IrProgram program{};
    program.entry_address = 0x2000;
    program.blocks.push_back(entry);

    jojo::Sh4ReferenceState state{};
    state.r[1] = 7;
    state.r[2] = 7;
    state.r[3] = 9;
    const auto run = jojo::execute_sh4_ir_reference(program, state, {}, 8);
    CHECK(run);
    if (!run) return;
    CHECK(!state.t);
    CHECK(state.pc == 0x2010u);
    CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::left_program);
}

static void test_direct_call_sets_pr_and_executes_delay_slot() {
    jojo::Sh4IrBlock entry{};
    entry.start_address = 0x3000;
    entry.exit = jojo::Sh4IrExit::direct_call;
    entry.branch_target = 0x3100;
    entry.fallthrough_target = 0x3004;
    entry.ops = {
        op(jojo::Sh4IrOp::call_direct, 0x3000, 0xFF, 0xFF, 0, 0x3100),
        op(jojo::Sh4IrOp::add_imm, 0x3002, 1, 0xFF, 1, 0, true),
    };
    jojo::Sh4IrProgram program{};
    program.entry_address = 0x3000;
    program.blocks.push_back(entry);

    jojo::Sh4ReferenceState state{};
    state.r[1] = 41;
    const auto run = jojo::execute_sh4_ir_reference(program, state, {}, 8);
    CHECK(run);
    if (!run) return;
    CHECK(state.r[1] == 42u);
    CHECK(state.pr == 0x3004u);
    CHECK(state.pc == 0x3100u);
}

static void test_indirect_call_latches_register_before_delay_slot() {
    jojo::Sh4IrBlock entry{};
    entry.start_address = 0x4000;
    entry.exit = jojo::Sh4IrExit::indirect_call;
    entry.fallthrough_target = 0x4004;
    entry.ops = {
        op(jojo::Sh4IrOp::call_reg, 0x4000, 0xFF, 2),
        op(jojo::Sh4IrOp::add_imm, 0x4002, 2, 0xFF, 4, 0, true),
    };
    jojo::Sh4IrProgram program{};
    program.entry_address = 0x4000;
    program.blocks.push_back(entry);

    jojo::Sh4ReferenceState state{};
    state.r[2] = 0x5000;
    const auto run = jojo::execute_sh4_ir_reference(program, state, {}, 8);
    CHECK(run);
    if (!run) return;
    CHECK(state.r[2] == 0x5004u);
    CHECK(state.pr == 0x4004u);
    CHECK(state.pc == 0x5000u);
}

static void test_pc_relative_memory_loads_are_bounded() {
    std::vector<std::uint8_t> bytes(12, 0);
    bytes[0] = 0x80;
    bytes[1] = 0xFF;
    bytes[4] = 0x78;
    bytes[5] = 0x56;
    bytes[6] = 0x34;
    bytes[7] = 0x12;

    auto program = one_block(0x6000, {
        op(jojo::Sh4IrOp::load_pc_word, 0x6000, 1, 0xFF, 0, 0x7000),
        op(jojo::Sh4IrOp::load_pc_long, 0x6002, 2, 0xFF, 0, 0x7004),
        op(jojo::Sh4IrOp::load_pc_address, 0x6004, 0, 0xFF, 0, 0x7008),
    });
    jojo::Sh4ReferenceState state{};
    jojo::Sh4ReferenceMemoryView memory{0x7000, bytes};
    const auto run = jojo::execute_sh4_ir_reference(program, state, memory, 8);
    CHECK(run);
    if (!run) return;
    CHECK(state.r[1] == 0xFFFFFF80u);
    CHECK(state.r[2] == 0x12345678u);
    CHECK(state.r[0] == 0x7008u);

    program.blocks[0].ops[1].target = 0x8000;
    state = {};
    const auto bad = jojo::execute_sh4_ir_reference(program, state, memory, 8);
    CHECK(!bad);
    if (!bad) CHECK(bad.error == jojo::ErrorCode::invalid_argument);
}

static void test_loop_guard_is_deterministic() {
    jojo::Sh4IrBlock loop{};
    loop.start_address = 0x8000;
    loop.exit = jojo::Sh4IrExit::direct_branch;
    loop.branch_target = 0x8000;
    loop.ops = {op(jojo::Sh4IrOp::branch_direct, 0x8000, 0xFF, 0xFF, 0, 0x8000)};
    jojo::Sh4IrProgram program{};
    program.entry_address = 0x8000;
    program.blocks.push_back(loop);
    jojo::Sh4ReferenceState state{};
    const auto run = jojo::execute_sh4_ir_reference(program, state, {}, 3);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::block_limit);
    CHECK(run.value.blocks_executed == 3u);
    CHECK(state.pc == 0x8000u);
}

int main() {
    test_integer_state_and_compare();
    test_data_memory_load_store_and_address_updates();
    test_delayed_conditional_branch_latches_decision();
    test_direct_call_sets_pr_and_executes_delay_slot();
    test_indirect_call_latches_register_before_delay_slot();
    test_pc_relative_memory_loads_are_bounded();
    test_loop_guard_is_deterministic();
    if (failures) {
        std::cerr << failures << " SH-4 reference executor assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 reference executor assertions passed\n";
    return 0;
}
