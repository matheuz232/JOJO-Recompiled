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

static const jojo::Sh4BasicBlock* block_at(const jojo::Sh4ControlFlowGraph& cfg, std::uint32_t address) {
    for (const auto& block : cfg.blocks) if (block.start_address == address) return &block;
    return nullptr;
}

static void test_partitions_conditional_and_delayed_branches() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xE001); // 1000 MOV #1,R0
    append_word(bytes, 0x8902); // 1002 BT -> 100A
    append_word(bytes, 0x7101); // 1004 ADD #1,R1
    append_word(bytes, 0xA001); // 1006 BRA -> 100C
    append_word(bytes, 0x0009); // 1008 delay slot
    append_word(bytes, 0x0009); // 100A branch target
    append_word(bytes, 0x000B); // 100C RTS
    append_word(bytes, 0x0009); // 100E delay slot

    const auto cfg = jojo::build_sh4_cfg(bytes, 0x1000, 0x1000);
    CHECK(cfg);
    if (!cfg) return;
    CHECK(cfg.value.blocks.size() == 4);

    const auto* entry = block_at(cfg.value, 0x1000);
    CHECK(entry != nullptr);
    if (entry) {
        CHECK(entry->instructions.size() == 2);
        CHECK(entry->exit == jojo::Sh4BlockExit::conditional_branch);
        CHECK(entry->branch_target.value_or(0) == 0x100Au);
        CHECK(entry->fallthrough_target.value_or(0) == 0x1004u);
    }

    const auto* fall = block_at(cfg.value, 0x1004);
    CHECK(fall != nullptr);
    if (fall) {
        CHECK(fall->instructions.size() == 3);
        CHECK(fall->instructions.back().address == 0x1008u);
        CHECK(fall->exit == jojo::Sh4BlockExit::direct_branch);
        CHECK(fall->branch_target.value_or(0) == 0x100Cu);
    }

    const auto* target = block_at(cfg.value, 0x100A);
    CHECK(target != nullptr);
    if (target) {
        CHECK(target->instructions.size() == 1);
        CHECK(target->exit == jojo::Sh4BlockExit::fallthrough);
        CHECK(target->fallthrough_target.value_or(0) == 0x100Cu);
    }

    const auto* ret = block_at(cfg.value, 0x100C);
    CHECK(ret != nullptr);
    if (ret) {
        CHECK(ret->instructions.size() == 2);
        CHECK(ret->exit == jojo::Sh4BlockExit::return_subroutine);
    }
}

static void test_records_calls_and_indirect_flow() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xB010); // 2000 BSR -> 2024 (outside stream)
    append_word(bytes, 0x0009); // 2002 delay
    append_word(bytes, 0x430B); // 2004 JSR @R3
    append_word(bytes, 0x0009); // 2006 delay
    append_word(bytes, 0x452B); // 2008 JMP @R5
    append_word(bytes, 0x0009); // 200A delay

    const auto cfg = jojo::build_sh4_cfg(bytes, 0x2000, 0x2000);
    CHECK(cfg);
    if (!cfg) return;
    CHECK(cfg.value.blocks.size() == 3);
    CHECK(cfg.value.direct_call_targets.size() == 1);
    CHECK(cfg.value.direct_call_targets[0] == 0x2024u);
    CHECK(cfg.value.indirect_call_sites.size() == 1);
    CHECK(cfg.value.indirect_call_sites[0] == 0x2004u);
    CHECK(cfg.value.indirect_jump_sites.size() == 1);
    CHECK(cfg.value.indirect_jump_sites[0] == 0x2008u);

    const auto* call = block_at(cfg.value, 0x2000);
    CHECK(call && call->exit == jojo::Sh4BlockExit::direct_call);
    CHECK(call && call->fallthrough_target.value_or(0) == 0x2004u);
    const auto* icall = block_at(cfg.value, 0x2004);
    CHECK(icall && icall->exit == jojo::Sh4BlockExit::indirect_call);
    CHECK(icall && icall->fallthrough_target.value_or(0) == 0x2008u);
    const auto* jump = block_at(cfg.value, 0x2008);
    CHECK(jump && jump->exit == jojo::Sh4BlockExit::indirect_jump);
}

static void test_validation() {
    std::vector<std::uint8_t> short_branch;
    append_word(short_branch, 0xA000); // needs delay slot, absent
    CHECK(!jojo::build_sh4_cfg(short_branch, 0x3000, 0x3000));

    std::vector<std::uint8_t> simple;
    append_word(simple, 0x0009);
    append_word(simple, 0x000B);
    append_word(simple, 0x0009);
    CHECK(!jojo::build_sh4_cfg(simple, 0x4000, 0x4001)); // unaligned entry
    CHECK(!jojo::build_sh4_cfg(simple, 0x4000, 0x5000)); // outside image

    std::vector<std::uint8_t> delay_target;
    append_word(delay_target, 0xA000); // 5000 BRA -> 5004, delay at 5002
    append_word(delay_target, 0x0009);
    append_word(delay_target, 0x8BFD); // 5004 BF -> 5002 (into prior delay slot)
    CHECK(!jojo::build_sh4_cfg(delay_target, 0x5000, 0x5000));
}

static void test_unsupported_terminates_block() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0xFFFF);
    append_word(bytes, 0x0009);
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x6000, 0x6000);
    CHECK(cfg);
    if (cfg) {
        CHECK(cfg.value.blocks.size() == 1);
        CHECK(cfg.value.blocks[0].exit == jojo::Sh4BlockExit::unsupported_instruction);
        CHECK(cfg.value.unsupported_sites.size() == 1);
        CHECK(cfg.value.unsupported_sites[0] == 0x6000u);
    }
}

int main() {
    test_partitions_conditional_and_delayed_branches();
    test_records_calls_and_indirect_flow();
    test_validation();
    test_unsupported_terminates_block();
    if (failures) {
        std::cerr << failures << " SH-4 CFG assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 CFG assertions passed\n";
    return 0;
}
