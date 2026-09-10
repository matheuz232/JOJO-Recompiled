#include "core/ps1_installation.h"
#include "core/ps1_boot_report.h"
#include "core/runtime.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::vector<std::string> regular_files_under(const fs::path& root) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec) {
            files.push_back(fs::relative(it->path(), root, ec).generic_string());
            if (ec) break;
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

int main() {
    const auto root = fs::temp_directory_path() / "jojo-ps1-local-evidence";
    const auto source = root / "game.iso";
    const auto install = root / "install";
    const auto report_path = root / "diagnostics" / "m3a-checkpoint.txt";

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    auto disc = test_ps1::make_disc_fixture();
    disc.executable = test_ps1::make_psx_exe_from_words({
        0x08000028u, // j 0x800000A0 (synthetic unknown BIOS selector)
        0x00000000u, // delay-slot nop
    });
    test_ps1::write_cooked_iso(source, disc);

    jojo::ConversionOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(disc));
    const auto converted = jojo::convert_image(source, install, options);
    CHECK(converted);

    const auto resolved = jojo::resolve_active_install_generation(install);
    CHECK(resolved);
    if (!resolved) {
        fs::remove_all(root, ec);
        return 1;
    }

    const auto manifest_path = resolved.value.manifest_path;
    const auto manifest_before = read_text(manifest_path);
    const auto files_before = regular_files_under(resolved.value.generation_dir);

    const auto evidence_options = jojo::ps1_local_evidence_options();
    CHECK(evidence_options.instruction_budget == std::numeric_limits<std::uint64_t>::max());
    CHECK(evidence_options.trace_capacity == 512u);
    CHECK(evidence_options.diagnostic_mmio_probe);
    CHECK(evidence_options.mmio_event_capacity == 1024u);

    const auto checkpoint = jojo::bootstrap_runtime_local_evidence_to_file(
        install, report_path);
    CHECK(checkpoint);
    if (checkpoint) {
        CHECK(checkpoint.value.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
        CHECK(checkpoint.value.instructions_retired == 2u);
        CHECK(checkpoint.value.recent_trace.size() == 2u);
        CHECK(checkpoint.value.diagnostic_probe_mode);
        CHECK(checkpoint.value.speculative_mmio_count == 0u);
        CHECK(checkpoint.value.bios_call_count == 1u);
    }

    CHECK(fs::is_regular_file(report_path));
    const auto report = read_text(report_path);
    CHECK(report.find("format=jojo-mega-checkpoint-v1\n") == 0u);
    CHECK(report.find("instructions_retired=2\n") != std::string::npos);
    CHECK(report.find("trace_sample_count=2\n") != std::string::npos);
    CHECK(report.find("diagnostic_probe_mode=1\n") != std::string::npos);
    CHECK(report.find("speculative_mmio_count=0\n") != std::string::npos);
    CHECK(report.find("bios_last_table=0x000000a0\n") != std::string::npos);
    CHECK(report.find("PS-X EXE") == std::string::npos);
    CHECK(read_text(manifest_path) == manifest_before);
    CHECK(regular_files_under(resolved.value.generation_dir) == files_before);

    fs::remove_all(root, ec);
    if (failures) {
        std::cerr << failures << " PS1 local-evidence assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 local-evidence assertions passed\n";
    return 0;
}
