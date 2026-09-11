#include "core/ps1_max3_coverage.h"
#include "core/ps1_omega_coverage.h"

#include <cstdint>
#include <iostream>
#include <optional>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

namespace {

jojo::Ps1BootReport sample_report(bool reverse = false) {
    jojo::Ps1BootReport report{};
    report.recent_trace.push_back({0x80010000u, 0x24020001u});
    report.recent_trace.push_back({0x80010004u, 0x30430020u});
    if (reverse) {
        const auto first = report.recent_trace.front();
        report.recent_trace.pop_front();
        report.recent_trace.push_back(first);
    }

    report.recent_bios_calls.push_back({0x80010010u, 0x000000b0u, 0x17u});
    report.recent_mmio.push_back({0x80010020u, 0x1f801070u, 4u, false, 0u, false});
    report.recent_cdrom_commands.push_back({0x01u, 0u, 2u});
    report.cpu_diagnostic = jojo::R3000aDiagnostic{
        jojo::R3000aBoundaryCode::unsupported_address_space,
        jojo::R3000aStage::memory,
        0x80010020u,
        0x8c620000u,
        0x1f801070u,
        std::nullopt,
        4u,
        std::nullopt,
        std::nullopt,
        std::nullopt,
    };
    return report;
}

jojo::Ps1Max3Frontier sample_frontier() {
    jojo::Ps1Max3Frontier frontier{};
    frontier.index = 42u;
    frontier.evidence = jojo::Ps1Max3EvidenceClass::strict;
    frontier.kind = jojo::Ps1Max3FrontierKind::mmio_read;
    frontier.pc = 0x80010020u;
    frontier.opcode = 0x8c620000u;
    frontier.address = 0x1f801070u;
    frontier.width = 4u;
    frontier.write = false;
    return frontier;
}

void test_new_semantic_items_then_repeats_are_zero_delta() {
    jojo::Ps1Max3Coverage coverage;
    auto report = sample_report();
    const auto frontier = sample_frontier();

    const auto first = coverage.observe(report, &frontier, 0x1234u);
    CHECK(first.new_states == 1u);
    CHECK(first.new_pcs == 4u);
    CHECK(first.new_opcodes >= 2u);
    CHECK(first.new_bios_pairs == 1u);
    CHECK(first.new_mmio_tuples == 1u);
    CHECK(first.new_cd_contexts == 1u);
    CHECK(first.new_cpu_classes == 1u);
    CHECK(first.new_frontiers == 1u);

    const auto repeated = coverage.observe(report, &frontier, 0x1234u);
    CHECK(repeated.new_states == 0u);
    CHECK(repeated.new_pcs == 0u);
    CHECK(repeated.new_opcodes == 0u);
    CHECK(repeated.new_bios_pairs == 0u);
    CHECK(repeated.new_mmio_tuples == 0u);
    CHECK(repeated.new_cd_contexts == 0u);
    CHECK(repeated.new_cpu_classes == 0u);
    CHECK(repeated.new_frontiers == 0u);
    CHECK(repeated.new_landmarks == 0u);
}

void test_progress_landmarks_record_first_seen_and_maxima() {
    jojo::Ps1Max3Coverage coverage;
    jojo::Ps1BootReport report{};
    report.interrupts_accepted = 1u;
    report.cdrom_command_count = 2u;
    report.dma_transfer_count = 3u;
    report.gpu_gp0_command_count = 4u;
    report.gpu_gp1_command_count = 5u;
    report.vram_write_count = 6u;
    report.presented_frames = 7u;

    const auto first = coverage.observe(report, nullptr, std::nullopt);
    CHECK(first.new_landmarks == 7u);
    const auto landmarks = coverage.landmarks();
    CHECK(landmarks.max_interrupts_accepted == 1u);
    CHECK(landmarks.max_cdrom_command_count == 2u);
    CHECK(landmarks.max_dma_transfer_count == 3u);
    CHECK(landmarks.max_gpu_gp0_command_count == 4u);
    CHECK(landmarks.max_gpu_gp1_command_count == 5u);
    CHECK(landmarks.max_vram_write_count == 6u);
    CHECK(landmarks.max_presented_frames == 7u);

    report.interrupts_accepted = 9u;
    report.presented_frames = 11u;
    const auto later = coverage.observe(report, nullptr, std::nullopt);
    CHECK(later.new_landmarks == 0u);
    CHECK(coverage.landmarks().max_interrupts_accepted == 9u);
    CHECK(coverage.landmarks().max_presented_frames == 11u);
}

void test_frontier_identity_is_semantic_not_index_based() {
    jojo::Ps1Max3Coverage coverage;
    jojo::Ps1BootReport report{};
    auto first = sample_frontier();
    auto same = first;
    same.index = 999u;
    same.evidence = jojo::Ps1Max3EvidenceClass::speculative;

    CHECK(coverage.observe(report, &first, std::nullopt).new_frontiers == 1u);
    CHECK(coverage.observe(report, &same, std::nullopt).new_frontiers == 0u);
}

void test_fingerprint_is_independent_of_observation_order() {
    jojo::Ps1Max3Coverage a;
    jojo::Ps1Max3Coverage b;
    auto normal = sample_report(false);
    auto reversed = sample_report(true);
    const auto frontier = sample_frontier();

    (void)a.observe(normal, &frontier, 0xabcdu);
    (void)b.observe(reversed, &frontier, 0xabcdu);
    CHECK(a.deterministic_hash() == b.deterministic_hash());
}

void test_omega_coverage_merges_across_epochs_deterministically() {
    jojo::Ps1OmegaCoverage first;
    first.observe_pc(0x80010000u);
    first.observe_pc(0x80010004u);
    first.observe_edge(0x80010000u, 0x80010004u);
    first.observe_mmio(0x1F801802u, 1u, true);

    jojo::Ps1OmegaCoverage second;
    second.observe_pc(0x80010004u);
    second.observe_pc(0x80010008u);
    second.observe_edge(0x80010004u, 0x80010008u);
    second.observe_mmio(0x1F801802u, 1u, true);
    second.observe_mmio(0x1F801803u, 1u, true);

    jojo::Ps1OmegaCoverage merged = first;
    merged.merge(second);
    CHECK(merged.unique_pc_count() == 3u);
    CHECK(merged.unique_edge_count() == 2u);
    CHECK(merged.unique_mmio_count() == 2u);

    jojo::Ps1OmegaCoverage reverse = second;
    reverse.merge(first);
    CHECK(jojo::encode_ps1_omega_coverage(merged) ==
          jojo::encode_ps1_omega_coverage(reverse));
}

void test_omega_frame_first_keeps_strict_and_speculative_separate() {
    jojo::Ps1OmegaFrameFirstEvidence evidence;
    evidence.record(jojo::Ps1OmegaFrameFirstLandmark::commercial_frame,
                    jojo::Ps1Max3EvidenceClass::speculative, 100u);
    CHECK(!evidence.strict_value(jojo::Ps1OmegaFrameFirstLandmark::commercial_frame).has_value());
    CHECK(evidence.speculative_value(jojo::Ps1OmegaFrameFirstLandmark::commercial_frame) == 100u);

    evidence.record(jojo::Ps1OmegaFrameFirstLandmark::first_gp0,
                    jojo::Ps1Max3EvidenceClass::strict, 200u);
    evidence.record(jojo::Ps1OmegaFrameFirstLandmark::first_vram_write,
                    jojo::Ps1Max3EvidenceClass::strict, 300u);
    CHECK(evidence.strict_value(jojo::Ps1OmegaFrameFirstLandmark::first_gp0) == 200u);
    CHECK(evidence.strict_value(jojo::Ps1OmegaFrameFirstLandmark::first_vram_write) == 300u);
    CHECK(!evidence.speculative_value(jojo::Ps1OmegaFrameFirstLandmark::first_gp0).has_value());
}

void test_omega_loop_observations_are_summarized() {
    jojo::Ps1OmegaCoverage coverage;
    for (std::uint64_t i = 0; i < 100u; ++i) {
        coverage.observe_execution(0x80020000u, 0x80020004u, 0x11223344u);
    }
    CHECK(coverage.execution_observation_count() == 100u);
    CHECK(coverage.hot_pc_count(0x80020000u) == 100u);
    CHECK(coverage.repeated_state_count() == 99u);
    CHECK(coverage.last_loop_period() == 1u);
    CHECK(coverage.unique_pc_count() == 2u);
    CHECK(coverage.unique_edge_count() == 1u);
}

} // namespace

int main() {
    test_new_semantic_items_then_repeats_are_zero_delta();
    test_progress_landmarks_record_first_seen_and_maxima();
    test_frontier_identity_is_semantic_not_index_based();
    test_fingerprint_is_independent_of_observation_order();
    test_omega_coverage_merges_across_epochs_deterministically();
    test_omega_frame_first_keeps_strict_and_speculative_separate();
    test_omega_loop_observations_are_summarized();
    return failures ? 1 : 0;
}
