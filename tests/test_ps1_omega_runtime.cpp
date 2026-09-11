#include "core/runtime.h"
#include "core/ps1_installation.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

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
    const auto root = fs::temp_directory_path() / "jojo-omega-runtime-wrapper";
    const auto source = root / "game.iso";
    const auto install = root / "install";
    const auto session = root / "diagnostics" / "omega-infinity";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const auto disc = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, disc);
    jojo::ConversionOptions conversion{};
    conversion.revision_profiles.push_back(test_ps1::make_revision_profile(disc));
    const auto converted = jojo::convert_image(source, install, conversion);
    CHECK(converted);
    if (!converted) return 1;

    const auto active = jojo::resolve_active_install_generation(install);
    CHECK(active);
    if (!active) return 1;
    const auto files_before = regular_files_under(active.value.generation_dir);

    auto options = jojo::ps1_omega_infinity_options();
    options.epoch_retired_limit = 64u;
    options.instruction_quantum = 1u;
    options.hot_trace_capacity = 8u;
    options.max_session_disk_bytes = 32ull * 1024ull * 1024ull;

    jojo::Ps1OmegaInfinityControl control;
    const auto result = jojo::bootstrap_runtime_omega_infinity(
        install, session, options, control,
        [&](const jojo::Ps1OmegaInfinityProgress& progress) {
            if (progress.total_retired >= 1u) control.request_stop();
        });
    CHECK(result);
    if (result) {
        CHECK(result.value.stop_reason == jojo::Ps1OmegaInfinityStopReason::user_requested ||
              result.value.stop_reason == jojo::Ps1OmegaInfinityStopReason::fatal_no_safe_continuation);
        CHECK(result.value.session_root == session);
    }
    CHECK(fs::is_directory(session));
    CHECK(regular_files_under(active.value.generation_dir) == files_before);

    fs::remove_all(root, ec);
    return failures ? 1 : 0;
}