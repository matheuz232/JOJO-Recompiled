#include "core/ps1_boot_report_io.h"
#include "core/ps1_max3_explorer.h"
#include "core/ps1_memory_bus.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Executable make_executable(const std::vector<std::uint32_t>& words) {
    auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(parsed);
    return parsed ? std::move(parsed.value) : jojo::Ps1Executable{};
}

static jojo::Ps1Max3Options fast_options() {
    jojo::Ps1Max3Options options{};
    options.max_nodes = 64u;
    options.max_branch_depth = 3u;
    options.max_total_retired = 10000u;
    options.segment_options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.segment_options.trace_capacity = 16u;
    options.segment_options.diagnostic_mmio_probe = true;
    options.segment_options.mmio_event_capacity = 32u;
    options.segment_options.bios_event_capacity = 32u;
    options.segment_options.stagnation_instruction_limit = 16u;
    return options;
}

static std::vector<std::uint32_t> one_frontier_then_loop() {
    return {
        test_mips::i(0x09u, 0u, 2u, 5u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::r(2u, 0u, 16u, 0u, 0x21u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    };
}

static void test_one_frontier_branches_four_ways() {
    const auto executable = make_executable(one_frontier_then_loop());
    auto options = fast_options();
    options.deep_frontier_enabled = false;
    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 5u);
    CHECK(report.nodes[0].depth == 0u);
    CHECK(report.nodes[0].stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(report.nodes[0].frontier_table == 0x000000A0u);
    CHECK(report.nodes[0].frontier_selector == 0x00000033u);
    CHECK(report.nodes[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
    CHECK(report.nodes[0].speculative_depth == 0u);

    const std::vector<jojo::Ps1BiosFallback> expected{
        jojo::Ps1BiosFallback::return_zero,
        jojo::Ps1BiosFallback::return_one,
        jojo::Ps1BiosFallback::return_minus_one,
        jojo::Ps1BiosFallback::preserve_v0,
    };
    std::vector<jojo::Ps1BiosFallback> seen;
    for (std::size_t i = 1; i < report.nodes.size(); ++i) {
        CHECK(report.nodes[i].depth == 1u);
        CHECK(report.nodes[i].fallback.has_value());
        CHECK(report.nodes[i].evidence == jojo::Ps1Max3EvidenceClass::speculative);
        CHECK(report.nodes[i].speculative_depth == 1u);
        CHECK(report.nodes[i].decision.has_value());
        if (report.nodes[i].decision) {
            CHECK(report.nodes[i].decision->kind == jojo::Ps1Max3DecisionKind::bios_fallback);
        }
        if (report.nodes[i].fallback) seen.push_back(*report.nodes[i].fallback);
    }
    CHECK(seen == expected);
    CHECK(report.dependencies.size() == 1u);
    CHECK(report.dependencies[0].kind == jojo::Ps1Max3DependencyKind::bios_frontier);
    CHECK(report.dependencies[0].table == 0x000000A0u);
    CHECK(report.dependencies[0].selector == 0x00000033u);

    const auto bios_frontier = std::find_if(report.frontiers.begin(), report.frontiers.end(), [](const auto& frontier) {
        return frontier.kind == jojo::Ps1Max3FrontierKind::bios &&
               frontier.table == 0x000000A0u && frontier.selector == 0x00000033u;
    });
    CHECK(bios_frontier != report.frontiers.end());
    if (bios_frontier != report.frontiers.end()) {
        CHECK(bios_frontier->evidence == jojo::Ps1Max3EvidenceClass::strict);
        CHECK(bios_frontier->expandable);
    }
}

static std::vector<std::uint32_t> two_frontiers_with_convergence() {
    return {
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 2u, 7u),
        test_mips::i(0x09u, 0u, 9u, 0x0034u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010020u >> 2),
        0x00000000u,
    };
}

static void test_converged_frontier_state_is_expanded_once() {
    const auto executable = make_executable(two_frontiers_with_convergence());
    auto options = fast_options();
    options.max_branch_depth = 2u;
    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 9u);
    CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.depth == 1u && node.deduplicated;
    }) == 3);
    CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.depth == 2u;
    }) == 4);
    CHECK(report.dependencies.size() == 2u);
    CHECK(report.dependencies[1].selector == 0x00000034u);
}

static std::vector<std::uint32_t> hle_state_splits_converged_frontier() {
    return {
        test_mips::i(0x09u, 0u, 2u, 5u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x05u, 2u, 0u, 6u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 4u, 0x4000u),
        test_mips::i(0x09u, 0u, 5u, 0x1000u),
        test_mips::i(0x09u, 0u, 9u, 0x0039u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 2u, 7u),
        test_mips::i(0x09u, 0u, 4u, 0u),
        test_mips::i(0x09u, 0u, 5u, 0u),
        test_mips::i(0x09u, 0u, 9u, 0x0034u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010048u >> 2),
        0x00000000u,
    };
}

static void test_hle_state_prevents_false_max3_frontier_deduplication() {
    const auto executable = make_executable(hle_state_splits_converged_frontier());
    auto options = fast_options();
    options.max_branch_depth = 2u;
    options.max_nodes = 32u;
    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 13u);
    CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.depth == 1u && node.deduplicated;
    }) == 2);
    CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.depth == 2u;
    }) == 8);
    CHECK(report.dependencies.size() == 2u);
    CHECK(report.dependencies[0].selector == 0x00000033u);
    CHECK(report.dependencies[1].selector == 0x00000034u);
}

static void test_bounds_and_progress_ranking_are_deterministic() {
    const auto executable = make_executable(one_frontier_then_loop());

    auto node_limited = fast_options();
    node_limited.max_nodes = 3u;
    const auto three = jojo::explore_ps1_max3(executable, node_limited);
    CHECK(three);
    if (three) CHECK(three.value.nodes.size() == 3u);

    auto depth_limited = fast_options();
    depth_limited.max_branch_depth = 0u;
    const auto root_only = jojo::explore_ps1_max3(executable, depth_limited);
    CHECK(root_only);
    if (root_only) CHECK(root_only.value.nodes.size() == 1u);

    auto retired_limited = fast_options();
    retired_limited.max_total_retired = 3u;
    const auto capped = jojo::explore_ps1_max3(executable, retired_limited);
    CHECK(capped);
    if (capped) {
        CHECK(capped.value.total_retired == 3u);
        CHECK(capped.value.nodes.size() == 1u);
    }

    const auto ranked_executable = make_executable({
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x05u, 2u, 0u, 3u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 9u, 0x0034u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010024u >> 2),
        0x00000000u,
    });
    auto ranked_options = fast_options();
    ranked_options.max_branch_depth = 2u;
    const auto ranked = jojo::explore_ps1_max3(ranked_executable, ranked_options);
    CHECK(ranked);
    if (ranked) {
        CHECK(ranked.value.best_node < ranked.value.nodes.size());
        if (ranked.value.best_node < ranked.value.nodes.size()) {
            CHECK(ranked.value.nodes[ranked.value.best_node].evidence ==
                  jojo::Ps1Max3EvidenceClass::strict);
        }
        CHECK(ranked.value.best_path.empty());
        CHECK(ranked.value.best_report.stop_reason ==
              jojo::Ps1BootStopReason::bios_call_unimplemented);
        CHECK(std::any_of(ranked.value.nodes.begin(), ranked.value.nodes.end(), [](const auto& node) {
            return node.evidence == jojo::Ps1Max3EvidenceClass::speculative;
        }));
    }
}

static void test_terminal_gpu_mmio_is_recorded_as_dependency_with_value() {
    const auto executable = make_executable({
        test_mips::i(0x0Fu, 0u, 4u, 0xFF00u),
        test_mips::i(0x09u, 0u, 9u, 0x0049u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
    });
    auto options = fast_options();
    options.max_branch_depth = 0u;
    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    CHECK(report.nodes[0].stop_reason == jojo::Ps1BootStopReason::gpu_command_unimplemented);
    CHECK(report.dependencies.size() == 1u);
    if (!report.dependencies.empty()) {
        CHECK(report.dependencies[0].address == 0x1F801810u);
        CHECK(report.dependencies[0].width == 4u);
        CHECK(report.dependencies[0].write);
    }
    CHECK(report.frontiers.size() == 1u);
    if (!report.frontiers.empty()) {
        CHECK(report.frontiers[0].kind == jojo::Ps1Max3FrontierKind::gpu_command);
        CHECK(report.frontiers[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
        CHECK(report.frontiers[0].write);
        CHECK(!report.frontiers[0].expandable);
        CHECK(report.nodes[0].expansion_stop == jojo::Ps1Max3ExpansionStop::terminal_frontier);
    }
    const auto text = jojo::format_ps1_max3_report(report);
    CHECK(text.find("dependency_0_kind=terminal_mmio") != std::string::npos);
    CHECK(text.find("dependency_0_value=0xff000000") != std::string::npos);
}

static void test_terminal_mmio_write_is_classified_without_branching() {
    const auto executable = make_executable({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1802u),
        test_mips::i(0x09u, 0u, 9u, 0x0055u),
        test_mips::i(0x28u, 8u, 9u, 0u),
    });
    auto options = fast_options();
    options.deep_frontier_enabled = false;
    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;
    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    CHECK(report.frontiers.size() == 1u);
    if (!report.frontiers.empty()) {
        CHECK(report.frontiers[0].kind == jojo::Ps1Max3FrontierKind::terminal_mmio_write);
        CHECK(report.frontiers[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
        CHECK(report.frontiers[0].write);
        CHECK(!report.frontiers[0].expandable);
    }
    CHECK(report.nodes[0].expansion_stop == jojo::Ps1Max3ExpansionStop::terminal_frontier);
}

static std::vector<std::uint32_t> observed_cdrom_sequence_program() {
    return {
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1800u),
        test_mips::i(0x09u, 0u, 9u, 0x0001u),
        test_mips::i(0x28u, 8u, 9u, 0x0000u),
        test_mips::i(0x24u, 8u, 10u, 0x0003u),
        test_mips::i(0x09u, 0u, 11u, 0x0000u),
        test_mips::i(0x28u, 8u, 11u, 0x0000u),
        test_mips::i(0x28u, 8u, 11u, 0x0003u),
        test_mips::i(0x28u, 8u, 11u, 0x0000u),
        test_mips::i(0x28u, 8u, 9u, 0x0001u),
        test_mips::j(0x02u, 0x80010028u >> 2),
        0x00000000u,
    };
}

static void apply_observed_cdrom_sequence(jojo::Ps1MemoryBus& bus) {
    bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    CHECK(bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    const auto hintsts = bus.read8(0x1F801803u);
    CHECK(hintsts.status == jojo::R3000aBusStatus::ok);
    CHECK(hintsts.value == 0xE0u);
    CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801803u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
}

static void test_observed_cdrom_sequence_is_real_max3_progress() {
    const auto executable = make_executable(observed_cdrom_sequence_program());
    auto options = fast_options();
    options.max_branch_depth = 0u;
    options.max_nodes = 4u;

    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    if (!report.nodes.empty()) {
        CHECK(report.nodes[0].path_cdrom_command_count == 1u);
    }
    CHECK(report.best_report.cdrom_command_count == 1u);
    CHECK(std::none_of(report.dependencies.begin(), report.dependencies.end(), [](const auto& dependency) {
        if (dependency.kind != jojo::Ps1Max3DependencyKind::speculative_mmio) return false;
        return dependency.address == 0x1F801800u ||
               dependency.address == 0x1F801801u ||
               dependency.address == 0x1F801803u;
    }));

    auto no_history_options = options;
    no_history_options.segment_options.mmio_event_capacity = 0u;
    const auto no_history = jojo::explore_ps1_max3(executable, no_history_options);
    CHECK(no_history);
    if (no_history && !no_history.value.nodes.empty()) {
        CHECK(no_history.value.best_report.cdrom_command_count == 1u);
        CHECK(no_history.value.nodes[0].path_cdrom_command_count == 1u);
    }

    jojo::Ps1MemoryBus left;
    jojo::Ps1MemoryBus right;
    apply_observed_cdrom_sequence(left);
    apply_observed_cdrom_sequence(right);
    CHECK(left.diagnostic_state_hash() == right.diagnostic_state_hash());
    const auto result = left.read8(0x1F801801u);
    CHECK(result.status == jojo::R3000aBusStatus::ok);
    CHECK(result.value == 0x02u);
    CHECK(left.diagnostic_state_hash() != right.diagnostic_state_hash());
}

constexpr std::uint32_t mtc0(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) | (0x04u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static void append_load32(std::vector<std::uint32_t>& words,
                          std::uint8_t reg,
                          std::uint32_t value) {
    words.push_back(test_mips::i(0x0Fu, 0u, reg,
        static_cast<std::uint16_t>(value >> 16u)));
    words.push_back(test_mips::i(0x0Du, reg, reg,
        static_cast<std::uint16_t>(value)));
}

static std::vector<std::uint32_t> interrupt_continuation_max3_program() {
    constexpr std::uint32_t node = 0x80001000u;
    constexpr std::uint32_t callback = 0x80012000u;
    std::vector<std::uint32_t> words;

    append_load32(words, 6u, callback);
    const std::vector<std::uint32_t> callback_words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1070u),
        test_mips::i(0x2Bu, 8u, 0u, 0u),
        test_mips::i(0x09u, 0u, 9u, 0x17u),
        test_mips::j(0x02u, 0x000000B0u >> 2),
        0x00000000u,
    };
    for (std::size_t i = 0u; i < callback_words.size(); ++i) {
        append_load32(words, 7u, callback_words[i]);
        words.push_back(test_mips::i(0x2Bu, 6u, 7u,
            static_cast<std::uint16_t>(i * 4u)));
    }

    append_load32(words, 5u, node);
    words.push_back(test_mips::i(0x2Bu, 5u, 0u, 4u));
    append_load32(words, 7u, callback);
    words.push_back(test_mips::i(0x2Bu, 5u, 7u, 8u));
    words.push_back(test_mips::i(0x09u, 0u, 4u, 2u));
    words.push_back(test_mips::i(0x09u, 0u, 9u, 2u));
    words.push_back(test_mips::j(0x03u, 0x000000C0u >> 2));
    words.push_back(0x00000000u);

    append_load32(words, 8u, 0x1F801074u);
    words.push_back(test_mips::i(0x09u, 0u, 10u, 4u));
    words.push_back(test_mips::i(0x2Bu, 8u, 10u, 0u));
    words.push_back(test_mips::i(0x09u, 0u, 13u, 0x0401u));
    words.push_back(mtc0(13u, 12u));

    append_load32(words, 11u, 0x1F801800u);
    words.push_back(test_mips::i(0x28u, 11u, 0u, 0u));
    words.push_back(test_mips::i(0x09u, 0u, 9u, 0x35u));
    words.push_back(test_mips::i(0x09u, 0u, 12u, 1u));
    words.push_back(test_mips::i(0x28u, 11u, 12u, 1u));

    const auto loop_pc = 0x80010000u + static_cast<std::uint32_t>(words.size() * 4u);
    words.push_back(test_mips::j(0x02u, loop_pc >> 2));
    words.push_back(0x00000000u);
    return words;
}

static void test_interrupt_continuation_removes_fake_a035_max3_frontier() {
    const auto executable = make_executable(interrupt_continuation_max3_program());
    auto options = fast_options();
    options.max_nodes = 8u;
    options.max_branch_depth = 2u;
    options.max_total_retired = 2000u;
    options.segment_options.stagnation_instruction_limit = 32u;

    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    CHECK(report.best_report.interrupts_accepted == 1u);
    CHECK(report.best_report.cdrom_command_count == 1u);
    CHECK(std::none_of(report.dependencies.begin(), report.dependencies.end(), [](const auto& dependency) {
        return dependency.kind == jojo::Ps1Max3DependencyKind::bios_frontier &&
               dependency.table == 0x000000A0u &&
               dependency.selector == 0x35u;
    }));
}

static void test_best_report_authority_requires_strict_best_node() {
    jojo::Ps1Max3Report speculative{};
    speculative.nodes.resize(1u);
    speculative.nodes[0].evidence = jojo::Ps1Max3EvidenceClass::speculative;
    speculative.best_node = 0u;
    speculative.best_report.stop_reason =
        jojo::Ps1BootStopReason::commercial_frame_presented;
    CHECK(!jojo::ps1_max3_best_is_strict_authoritative(speculative));

    jojo::Ps1Max3Report strict = speculative;
    strict.nodes[0].evidence = jojo::Ps1Max3EvidenceClass::strict;
    CHECK(jojo::ps1_max3_best_is_strict_authoritative(strict));
}

int main() {
    test_one_frontier_branches_four_ways();
    test_converged_frontier_state_is_expanded_once();
    test_hle_state_prevents_false_max3_frontier_deduplication();
    test_bounds_and_progress_ranking_are_deterministic();
    test_terminal_gpu_mmio_is_recorded_as_dependency_with_value();
    test_terminal_mmio_write_is_classified_without_branching();
    test_observed_cdrom_sequence_is_real_max3_progress();
    test_interrupt_continuation_removes_fake_a035_max3_frontier();
    test_best_report_authority_requires_strict_best_node();
    return failures ? 1 : 0;
}
