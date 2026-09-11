#include "core/ps1_omega_coverage.h"
#include "core/ps1_omega_session_io.h"
#include "core/ps1_omega_summary.h"
#include "core/sha256.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::vector<std::uint8_t> bytes(std::string text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

int main() {
    const auto root = fs::temp_directory_path() / "jojo-omega-summary-test";
    const auto zip_a = root.parent_path() / "jojo-omega-summary-a.zip";
    const auto zip_b = root.parent_path() / "jojo-omega-summary-b.zip";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove(zip_a, ec);
    fs::remove(zip_b, ec);

    jojo::Ps1OmegaEvidenceRecorder recorder(root, 32ull * 1024ull * 1024ull);
    CHECK(recorder.ready());

    const auto strict = bytes(
        "format=jojo-omega-infinity-segment-v1\n"
        "epoch=1\n"
        "evidence=strict\n"
        "state_hash=0x1111111111111111\n"
        "depth=0\n"
        "speculative_depth=0\n"
        "assumption_count=0\n"
        "path_count=0\n"
        "format=jojo-m3a-checkpoint-v1\n"
        "stop_reason=mmio_unimplemented\n"
        "instructions_retired=123\n"
        "last_pc=0x80010020\n"
        "gpu_gp0_command_count=1\n"
        "gpu_gp1_command_count=3\n"
        "dma_transfer_count=0\n"
        "vram_write_count=0\n"
        "presented_frames=0\n"
        "mmio_last_address=0x1f801802\n"
        "mmio_last_width=1\n"
        "mmio_last_write=1\n"
        "mmio_last_value=0x00000007\n");
    CHECK(recorder.append(jojo::Ps1OmegaEvidenceCategory::cpu, 1u, strict));

    const auto speculative = bytes(
        "format=jojo-omega-infinity-segment-v1\n"
        "epoch=2\n"
        "evidence=speculative\n"
        "state_hash=0x2222222222222222\n"
        "depth=1\n"
        "speculative_depth=1\n"
        "assumption_count=1\n"
        "path_count=1\n"
        "format=jojo-m3a-checkpoint-v1\n"
        "stop_reason=cpu_boundary\n"
        "instructions_retired=456\n"
        "last_pc=0x80020040\n"
        "gpu_gp0_command_count=4\n"
        "gpu_gp1_command_count=5\n"
        "dma_transfer_count=1\n"
        "vram_write_count=2\n"
        "presented_frames=0\n");
    CHECK(recorder.append(jojo::Ps1OmegaEvidenceCategory::cpu, 2u, speculative));

    jojo::Ps1OmegaCoverage coverage;
    coverage.observe_pc(0x80010000u);
    coverage.observe_edge(0x80010000u, 0x80010004u);
    coverage.observe_mmio(0x1F801802u, 1u, true);
    const auto coverage_bytes = jojo::encode_ps1_omega_coverage(coverage);
    CHECK(recorder.append(jojo::Ps1OmegaEvidenceCategory::coverage, 2u, coverage_bytes));

    jojo::Ps1OmegaInfinitySummary run{};
    run.epoch_count = 2u;
    run.total_retired = 579u;
    run.strict_frontier_count = 1u;
    run.speculative_frontier_count = 1u;
    run.unique_state_count = 2u;
    run.presented_frames = 0u;
    run.stop_reason = jojo::Ps1OmegaInfinityStopReason::user_requested;
    run.session_root = root;

    const auto first = jojo::finalize_ps1_omega_bundle(root, "0123456789abcdef", run);
    CHECK(first);
    if (!first) return 1;
    const auto summary_a = read_text(first.value.summary_path);
    const auto manifest_a = read_text(first.value.manifest_path);

    const auto second = jojo::finalize_ps1_omega_bundle(root, "0123456789abcdef", run);
    CHECK(second);
    CHECK(read_text(second.value.summary_path) == summary_a);
    CHECK(read_text(second.value.manifest_path) == manifest_a);

    CHECK(summary_a.find("format=jojo-omega-infinity-summary-v1\n") == 0u);
    CHECK(summary_a.find("schema_version=1\n") != std::string::npos);
    CHECK(summary_a.find("executable_identity=0123456789abcdef\n") != std::string::npos);
    CHECK(summary_a.find("epoch_count=2\n") != std::string::npos);
    CHECK(summary_a.find("total_retired=579\n") != std::string::npos);
    CHECK(summary_a.find("strict_frontier_count=1\n") != std::string::npos);
    CHECK(summary_a.find("speculative_frontier_count=1\n") != std::string::npos);
    CHECK(summary_a.find("strict_best_report_begin=1\n") != std::string::npos);
    CHECK(summary_a.find("strict_blocker_count=1\n") != std::string::npos);
    CHECK(summary_a.find("speculative_future_blocker_count=1\n") != std::string::npos);
    CHECK(summary_a.find("frame_first_strict_first_gp0=123\n") != std::string::npos);
    CHECK(summary_a.find("frame_first_strict_first_valid_display_config=unavailable_not_serialized\n") != std::string::npos);
    CHECK(summary_a.find("coverage_unique_pc_count=2\n") != std::string::npos);
    CHECK(summary_a.find("coverage_unique_edge_count=1\n") != std::string::npos);
    CHECK(summary_a.find("coverage_unique_mmio_count=1\n") != std::string::npos);
    CHECK(summary_a.find("stop_reason=user_requested\n") != std::string::npos);
    CHECK(summary_a.find("not_serialized_main_ram_page_deltas=1\n") != std::string::npos);

    CHECK(manifest_a.find("\"format\": \"jojo-omega-infinity-bundle-v1\"") != std::string::npos);
    CHECK(manifest_a.find("\"executable_identity\": \"0123456789abcdef\"") != std::string::npos);
    CHECK(manifest_a.find("\"events/cpu-") != std::string::npos);
    CHECK(manifest_a.find("\"sha256\":") != std::string::npos);

    CHECK(first.value.strict_frontier_count == 1u);
    CHECK(first.value.speculative_frontier_count == 1u);
    CHECK(first.value.unique_serialized_state_count == 2u);
    CHECK(first.value.coverage_unique_pc_count == 2u);
    CHECK(first.value.coverage_unique_edge_count == 1u);
    CHECK(first.value.coverage_unique_mmio_count == 1u);

    const auto packaged_a = jojo::package_ps1_omega_bundle_zip(root, zip_a);
    const auto packaged_b = jojo::package_ps1_omega_bundle_zip(root, zip_b);
    CHECK(packaged_a);
    CHECK(packaged_b);
    if (packaged_a && packaged_b) {
        const auto hash_a = jojo::sha256_file(zip_a);
        const auto hash_b = jojo::sha256_file(zip_b);
        CHECK(hash_a && hash_b);
        if (hash_a && hash_b) CHECK(hash_a.value == hash_b.value);
        std::ifstream zip(zip_a, std::ios::binary);
        char signature[4]{};
        zip.read(signature, 4);
        CHECK(signature[0] == 'P' && signature[1] == 'K' &&
              static_cast<unsigned char>(signature[2]) == 3u &&
              static_cast<unsigned char>(signature[3]) == 4u);
    }

    fs::remove_all(root, ec);
    fs::remove(zip_a, ec);
    fs::remove(zip_b, ec);
    return failures ? 1 : 0;
}