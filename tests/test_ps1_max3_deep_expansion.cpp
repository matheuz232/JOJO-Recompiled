#include "core/ps1_max3_explorer.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Executable make_executable(const std::vector<std::uint32_t>& words) {
    auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(parsed);
    return parsed ? std::move(parsed.value) : jojo::Ps1Executable{};
}

static jojo::Ps1Max3Options fast_options(jojo::Ps1Max3Profile profile) {
    auto options = jojo::ps1_max3_options(profile);
    options.max_nodes = 64u;
    options.max_branch_depth = 4u;
    options.max_speculative_depth = 4u;
    options.max_unique_frontiers = 64u;
    options.max_candidates_per_read = 3u;
    options.max_total_retired = 10000u;
    options.segment_options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.segment_options.trace_capacity = 32u;
    options.segment_options.diagnostic_mmio_probe = true;
    options.segment_options.mmio_event_capacity = 32u;
    options.segment_options.bios_event_capacity = 32u;
    options.segment_options.stagnation_instruction_limit = 32u;
    return options;
}

static void append_mmio_base(std::vector<std::uint32_t>& words) {
    words.push_back(test_mips::i(0x0Fu, 0u, 8u, 0x1F80u));
    words.push_back(test_mips::i(0x0Du, 8u, 8u, 0x1802u));
}

static std::vector<std::uint32_t> two_read_chain() {
    std::vector<std::uint32_t> words;
    append_mmio_base(words);
    words.push_back(test_mips::i(0x24u, 8u, 9u, 0u)); // LBU t1,0(t0)
    words.push_back(test_mips::i(0x24u, 8u, 10u, 0u)); // LBU t2,0(t0)
    words.push_back(test_mips::j(0x02u, 0x80010010u >> 2));
    words.push_back(0u);
    return words;
}

static std::vector<std::uint32_t> read_then_write() {
    std::vector<std::uint32_t> words;
    append_mmio_base(words);
    words.push_back(test_mips::i(0x24u, 8u, 9u, 0u));
    words.push_back(test_mips::i(0x09u, 0u, 10u, 0x55u));
    words.push_back(test_mips::i(0x28u, 8u, 10u, 0u)); // SB t2,0(t0)
    return words;
}

static std::vector<std::uint32_t> bios_then_read() {
    std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 9u, 0x33u),
        test_mips::i(0x09u, 0u, 10u, 0xA0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0u,
    };
    append_mmio_base(words);
    words.push_back(test_mips::i(0x24u, 8u, 11u, 0u));
    words.push_back(test_mips::j(0x02u, 0x8001001Cu >> 2));
    words.push_back(0u);
    return words;
}

static std::vector<std::uint32_t> read_then_bios() {
    std::vector<std::uint32_t> words;
    append_mmio_base(words);
    words.push_back(test_mips::i(0x24u, 8u, 11u, 0u));
    words.push_back(test_mips::i(0x09u, 0u, 9u, 0x34u));
    words.push_back(test_mips::i(0x09u, 0u, 10u, 0xA0u));
    words.push_back(test_mips::r(10u, 0u, 31u, 0u, 0x09u));
    words.push_back(0u);
    return words;
}

static std::vector<std::uint32_t> candidate_split_to_bios() {
    std::vector<std::uint32_t> words;
    append_mmio_base(words);
    words.push_back(test_mips::i(0x24u, 8u, 11u, 0u));
    words.push_back(0u); // retire load delay
    words.push_back(test_mips::i(0x04u, 11u, 0u, 3u)); // BEQ t3,zero -> selector 0x33 path
    words.push_back(0u);
    words.push_back(test_mips::i(0x09u, 0u, 9u, 0x34u));
    words.push_back(test_mips::j(0x02u, 0x80010024u >> 2));
    words.push_back(0u);
    words.push_back(test_mips::i(0x09u, 0u, 9u, 0x33u));
    words.push_back(test_mips::i(0x09u, 0u, 10u, 0xA0u));
    words.push_back(test_mips::r(10u, 0u, 31u, 0u, 0x09u));
    words.push_back(0u);
    return words;
}

static bool has_mmio_decision(const jojo::Ps1Max3Report& report) {
    return std::any_of(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.decision && node.decision->kind == jojo::Ps1Max3DecisionKind::mmio_read_fallback;
    });
}

static void test_strict_stops_but_deep_crosses_first_read() {
    const auto executable = make_executable(two_read_chain());
    const auto strict = jojo::explore_ps1_max3(executable, fast_options(jojo::Ps1Max3Profile::strict));
    CHECK(strict);
    if (strict) {
        CHECK(strict.value.nodes.size() == 1u);
        CHECK(!has_mmio_decision(strict.value));
        CHECK(strict.value.frontiers.size() == 1u);
        if (!strict.value.frontiers.empty()) {
            CHECK(strict.value.frontiers[0].kind == jojo::Ps1Max3FrontierKind::mmio_read);
            CHECK(strict.value.frontiers[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
        }
    }

    const auto deep = jojo::explore_ps1_max3(executable, fast_options(jojo::Ps1Max3Profile::deep));
    CHECK(deep);
    if (!deep) return;
    CHECK(deep.value.nodes.size() > 1u);
    CHECK(has_mmio_decision(deep.value));
    CHECK(std::any_of(deep.value.frontiers.begin(), deep.value.frontiers.end(), [](const auto& frontier) {
        return frontier.kind == jojo::Ps1Max3FrontierKind::mmio_read &&
               frontier.address == 0x1F801802u &&
               frontier.evidence == jojo::Ps1Max3EvidenceClass::speculative;
    }));
}

static void test_read_then_write_never_branches_through_write() {
    const auto executable = make_executable(read_then_write());
    const auto deep = jojo::explore_ps1_max3(executable, fast_options(jojo::Ps1Max3Profile::deep));
    CHECK(deep);
    if (!deep) return;
    CHECK(has_mmio_decision(deep.value));
    const auto write = std::find_if(deep.value.frontiers.begin(), deep.value.frontiers.end(), [](const auto& frontier) {
        return frontier.kind == jojo::Ps1Max3FrontierKind::terminal_mmio_write &&
               frontier.address == 0x1F801802u;
    });
    CHECK(write != deep.value.frontiers.end());
    if (write != deep.value.frontiers.end()) CHECK(!write->expandable);
}

static void test_bios_then_read_preserves_mixed_assumption_chain() {
    const auto executable = make_executable(bios_then_read());
    const auto deep = jojo::explore_ps1_max3(executable, fast_options(jojo::Ps1Max3Profile::deep));
    CHECK(deep);
    if (!deep) return;
    CHECK(std::any_of(deep.value.nodes.begin(), deep.value.nodes.end(), [](const auto& node) {
        return node.decision &&
               node.decision->kind == jojo::Ps1Max3DecisionKind::mmio_read_fallback &&
               node.speculative_depth >= 2u;
    }));
    CHECK(std::any_of(deep.value.frontiers.begin(), deep.value.frontiers.end(), [](const auto& frontier) {
        if (frontier.evidence != jojo::Ps1Max3EvidenceClass::speculative) return false;
        bool bios = false;
        for (const auto& decision : frontier.assumption_chain) {
            bios = bios || decision.kind == jojo::Ps1Max3DecisionKind::bios_fallback;
        }
        return bios;
    }));
}

static void test_read_then_bios_discovers_bios_frontier_downstream() {
    const auto executable = make_executable(read_then_bios());
    const auto deep = jojo::explore_ps1_max3(executable, fast_options(jojo::Ps1Max3Profile::deep));
    CHECK(deep);
    if (!deep) return;
    CHECK(std::any_of(deep.value.frontiers.begin(), deep.value.frontiers.end(), [](const auto& frontier) {
        return frontier.kind == jojo::Ps1Max3FrontierKind::bios &&
               frontier.selector == 0x34u &&
               frontier.evidence == jojo::Ps1Max3EvidenceClass::speculative &&
               !frontier.assumption_chain.empty() &&
               frontier.assumption_chain.front().kind == jojo::Ps1Max3DecisionKind::mmio_read_fallback;
    }));
}

static void test_candidate_runtime_copies_remain_isolated() {
    const auto executable = make_executable(candidate_split_to_bios());
    auto options = fast_options(jojo::Ps1Max3Profile::deep);
    options.max_branch_depth = 2u;
    options.max_speculative_depth = 2u;
    const auto deep = jojo::explore_ps1_max3(executable, options);
    CHECK(deep);
    if (!deep) return;
    bool saw_zero_path = false;
    bool saw_nonzero_path = false;
    for (const auto& frontier : deep.value.frontiers) {
        if (frontier.kind != jojo::Ps1Max3FrontierKind::bios) continue;
        if (frontier.selector == 0x33u) saw_zero_path = true;
        if (frontier.selector == 0x34u) saw_nonzero_path = true;
    }
    CHECK(saw_zero_path);
    CHECK(saw_nonzero_path);
}

int main() {
    test_strict_stops_but_deep_crosses_first_read();
    test_read_then_write_never_branches_through_write();
    test_bios_then_read_preserves_mixed_assumption_chain();
    test_read_then_bios_discovers_bios_frontier_downstream();
    test_candidate_runtime_copies_remain_isolated();
    return failures ? 1 : 0;
}
