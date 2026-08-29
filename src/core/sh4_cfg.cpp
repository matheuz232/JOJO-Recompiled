#include "core/sh4_cfg.h"
#include <algorithm>
#include <set>
#include <string>

namespace jojo {
namespace {

bool contains_address(std::uint32_t address,
                      std::uint32_t base,
                      std::size_t byte_count) noexcept {
    const auto begin = static_cast<std::uint64_t>(base);
    const auto end = begin + static_cast<std::uint64_t>(byte_count);
    const auto value = static_cast<std::uint64_t>(address);
    return value >= begin && value < end && ((value - begin) & 1u) == 0u;
}

std::size_t index_for(std::uint32_t address, std::uint32_t base) noexcept {
    return static_cast<std::size_t>((address - base) / 2u);
}

void push_sorted_unique(std::vector<std::uint32_t>& values, std::uint32_t value) {
    values.push_back(value);
}

void finalize_sorted_unique(std::vector<std::uint32_t>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

bool is_conditional(const Sh4Instruction& i) noexcept {
    return i.op == Sh4Op::bt || i.op == Sh4Op::bf ||
           i.op == Sh4Op::bt_s || i.op == Sh4Op::bf_s;
}

bool is_direct_unconditional_branch(const Sh4Instruction& i) noexcept {
    return i.op == Sh4Op::bra;
}

bool is_direct_call(const Sh4Instruction& i) noexcept {
    return i.op == Sh4Op::bsr;
}

Result<void> append_delay_slot(const std::vector<Sh4Instruction>& decoded,
                               std::size_t branch_index,
                               Sh4BasicBlock& block,
                               std::vector<std::uint32_t>& unsupported_sites) {
    if (branch_index + 1u >= decoded.size()) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "SH-4 control-flow instruction is missing its delay slot");
    }
    const auto& slot = decoded[branch_index + 1u];
    if (slot.is_branch) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "SH-4 branch/control instruction appears in a delay slot");
    }
    block.instructions.push_back(slot);
    if (slot.op == Sh4Op::unsupported) push_sorted_unique(unsupported_sites, slot.address);
    return Result<void>::success();
}

}

Result<Sh4ControlFlowGraph> build_sh4_cfg(const std::vector<std::uint8_t>& bytes,
                                          std::uint32_t base_address,
                                          std::uint32_t entry_address) {
    auto decoded_result = decode_sh4_stream(bytes, base_address);
    if (!decoded_result) {
        return Result<Sh4ControlFlowGraph>::failure(decoded_result.error, decoded_result.detail);
    }
    const auto& decoded = decoded_result.value;
    if (decoded.empty()) {
        return Result<Sh4ControlFlowGraph>::failure(
            ErrorCode::invalid_argument, "cannot build SH-4 CFG from an empty stream");
    }
    if (!contains_address(entry_address, base_address, bytes.size())) {
        return Result<Sh4ControlFlowGraph>::failure(
            ErrorCode::invalid_argument, "SH-4 CFG entry address is outside or unaligned to the stream");
    }

    std::set<std::uint32_t> leaders{entry_address};
    std::set<std::uint32_t> delay_slots;
    std::set<std::uint32_t> direct_branch_targets;

    for (const auto& i : decoded) {
        if (i.has_delay_slot && contains_address(i.address + 2u, base_address, bytes.size())) {
            delay_slots.insert(i.address + 2u);
        }

        if (is_direct_unconditional_branch(i) || is_conditional(i)) {
            if (const auto target = sh4_direct_target(i)) {
                if (contains_address(*target, base_address, bytes.size())) {
                    leaders.insert(*target);
                    direct_branch_targets.insert(*target);
                }
            }
        }

        if (is_conditional(i)) {
            const auto fallthrough = i.address + (i.has_delay_slot ? 4u : 2u);
            if (contains_address(fallthrough, base_address, bytes.size())) leaders.insert(fallthrough);
        } else if (is_direct_call(i) || i.op == Sh4Op::jsr_reg || i.op == Sh4Op::bsrf) {
            const auto fallthrough = i.address + 4u;
            if (contains_address(fallthrough, base_address, bytes.size())) leaders.insert(fallthrough);
        }
    }

    if (delay_slots.count(entry_address) != 0u) {
        return Result<Sh4ControlFlowGraph>::failure(
            ErrorCode::invalid_argument, "SH-4 CFG entry points into a delay slot");
    }
    for (const auto target : direct_branch_targets) {
        if (delay_slots.count(target) != 0u) {
            return Result<Sh4ControlFlowGraph>::failure(
                ErrorCode::invalid_installation,
                "SH-4 direct branch target points into a delay slot");
        }
    }

    Sh4ControlFlowGraph cfg{};
    cfg.base_address = base_address;
    cfg.entry_address = entry_address;

    std::set<std::uint32_t> pending{entry_address};
    std::set<std::uint32_t> visited;

    auto schedule_if_internal = [&](std::uint32_t address) {
        if (contains_address(address, base_address, bytes.size()) && visited.count(address) == 0u) {
            pending.insert(address);
        }
    };

    while (!pending.empty()) {
        const auto start = *pending.begin();
        pending.erase(pending.begin());
        if (!visited.insert(start).second) continue;

        Sh4BasicBlock block{};
        block.start_address = start;
        auto index = index_for(start, base_address);

        while (index < decoded.size()) {
            const auto& i = decoded[index];
            if (i.address != start && leaders.count(i.address) != 0u) {
                block.exit = Sh4BlockExit::fallthrough;
                block.fallthrough_target = i.address;
                schedule_if_internal(i.address);
                break;
            }

            block.instructions.push_back(i);

            if (i.op == Sh4Op::unsupported) {
                block.exit = Sh4BlockExit::unsupported_instruction;
                push_sorted_unique(cfg.unsupported_sites, i.address);
                break;
            }

            if (is_conditional(i)) {
                if (i.has_delay_slot) {
                    const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                    if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                }
                block.exit = Sh4BlockExit::conditional_branch;
                block.branch_target = sh4_direct_target(i);
                block.fallthrough_target = i.address + (i.has_delay_slot ? 4u : 2u);
                if (block.branch_target) schedule_if_internal(*block.branch_target);
                schedule_if_internal(*block.fallthrough_target);
                break;
            }

            if (i.op == Sh4Op::bra) {
                const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                block.exit = Sh4BlockExit::direct_branch;
                block.branch_target = sh4_direct_target(i);
                if (block.branch_target) schedule_if_internal(*block.branch_target);
                break;
            }

            if (i.op == Sh4Op::bsr) {
                const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                block.exit = Sh4BlockExit::direct_call;
                block.branch_target = sh4_direct_target(i);
                if (block.branch_target) push_sorted_unique(cfg.direct_call_targets, *block.branch_target);
                block.fallthrough_target = i.address + 4u;
                schedule_if_internal(*block.fallthrough_target);
                break;
            }

            if (i.op == Sh4Op::jsr_reg || i.op == Sh4Op::bsrf) {
                const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                block.exit = Sh4BlockExit::indirect_call;
                push_sorted_unique(cfg.indirect_call_sites, i.address);
                block.fallthrough_target = i.address + 4u;
                schedule_if_internal(*block.fallthrough_target);
                break;
            }

            if (i.op == Sh4Op::jmp_reg || i.op == Sh4Op::braf) {
                const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                block.exit = Sh4BlockExit::indirect_jump;
                push_sorted_unique(cfg.indirect_jump_sites, i.address);
                break;
            }

            if (i.op == Sh4Op::rts) {
                const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                block.exit = Sh4BlockExit::return_subroutine;
                break;
            }

            if (i.op == Sh4Op::rte) {
                const auto delay = append_delay_slot(decoded, index, block, cfg.unsupported_sites);
                if (!delay) return Result<Sh4ControlFlowGraph>::failure(delay.error, delay.detail);
                block.exit = Sh4BlockExit::return_exception;
                break;
            }

            ++index;
            if (index >= decoded.size()) {
                block.exit = Sh4BlockExit::end_of_stream;
                break;
            }
        }

        cfg.blocks.push_back(std::move(block));
    }

    std::sort(cfg.blocks.begin(), cfg.blocks.end(), [](const Sh4BasicBlock& a, const Sh4BasicBlock& b) {
        return a.start_address < b.start_address;
    });
    finalize_sorted_unique(cfg.direct_call_targets);
    finalize_sorted_unique(cfg.indirect_call_sites);
    finalize_sorted_unique(cfg.indirect_jump_sites);
    finalize_sorted_unique(cfg.unsupported_sites);
    return Result<Sh4ControlFlowGraph>::success(std::move(cfg));
}

}
