#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "core/ps1_max3_explorer.h"
#include "core/ps1_omega_infinity.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Executable make_executable(const std::vector<std::uint32_t>& words) {
    auto executable = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(executable);
    return executable ? std::move(executable.value) : jojo::Ps1Executable{};
}

static jojo::Ps1BootRuntime make_runtime(const std::vector<std::uint32_t>& words) {
    auto executable = make_executable(words);
    auto runtime = jojo::Ps1BootRuntime::create(executable);
    CHECK(runtime);
    return runtime ? std::move(runtime.value) : jojo::Ps1BootRuntime{};
}

static void test_infinity_defaults() {
    const auto options = jojo::ps1_omega_infinity_options();
    CHECK(options.epoch_retired_limit == 3000000000ull);
    CHECK(options.chunk_target_bytes == 8ull * 1024ull * 1024ull);
    CHECK(options.max_session_disk_bytes == 16ull * 1024ull * 1024ull * 1024ull);
    CHECK(options.hot_trace_capacity == 262144u);
    CHECK(options.stop_on_strict_commercial_frame);

    jojo::Ps1OmegaInfinityControl control;
    CHECK(!control.stop_requested());
    control.request_stop();
    CHECK(control.stop_requested());

    const auto deep = jojo::ps1_max3_local_evidence_options();
    CHECK(deep.max_total_retired == 1000000000ull);
    const auto omega = jojo::ps1_max3_options(jojo::Ps1Max3Profile::omega);
    CHECK(omega.max_total_retired == 3000000000ull);
}

static void test_epoch_rollover_depends_only_on_retired_limit() {
    auto options = jojo::ps1_omega_infinity_options();
    options.epoch_retired_limit = 7u;
    CHECK(!jojo::ps1_omega_infinity_epoch_complete(0u, options));
    CHECK(!jojo::ps1_omega_infinity_epoch_complete(6u, options));
    CHECK(jojo::ps1_omega_infinity_epoch_complete(7u, options));
    CHECK(jojo::ps1_omega_infinity_epoch_complete(8u, options));
}

static void test_terminal_mmio_write_can_continue_without_device_effect() {
    auto runtime = make_runtime({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1802u),
        test_mips::i(0x09u, 0u, 9u, 0x0007u),
        test_mips::i(0x28u, 8u, 9u, 0u),
        test_mips::i(0x09u, 0u, 16u, 0x1234u),
    });

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 32u;
    options.trace_capacity = 16u;
    options.diagnostic_mmio_probe = true;
    options.mmio_event_capacity = 16u;
    const auto strict = runtime.run(options);
    CHECK(strict.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(strict.instructions_retired == 3u);
    CHECK(strict.last_pc == 0x8001000Cu);
    CHECK(strict.unsupported_access.has_value());
    if (strict.unsupported_access) {
        CHECK(strict.unsupported_access->physical_address == 0x1F801802u);
        CHECK(strict.unsupported_access->width == 1u);
        CHECK(strict.unsupported_access->write);
        CHECK(strict.unsupported_access->value == 0x07u);
    }

    const auto bus_before = runtime.bus().diagnostic_state_hash();
    CHECK(runtime.apply_diagnostic_mmio_write_no_effect(strict));
    CHECK(runtime.bus().diagnostic_state_hash() == bus_before);
    CHECK(runtime.cpu_state().pc == 0x80010010u);
    CHECK(!runtime.apply_diagnostic_mmio_write_no_effect(strict));

    jojo::Ps1BootOptions one{};
    one.instruction_budget = 1u;
    const auto resumed = runtime.run(one);
    CHECK(resumed.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(resumed.instructions_retired == 1u);
    CHECK(runtime.cpu_state().gpr[16] == 0x00001234u);
}

static void test_multi_epoch_stop_resume_and_identity_rejection() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "jojo-omega-infinity-session-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    // Infinite control flow with an evolving register so exact-state deduplication
    // does not intentionally collapse the fixture before the epoch boundary.
    const auto executable = make_executable({
        test_mips::i(0x09u, 2u, 2u, 1u),
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });

    auto options = jojo::ps1_omega_infinity_options();
    options.epoch_retired_limit = 2u;
    options.instruction_quantum = 1u;
    options.hot_trace_capacity = 8u;
    options.max_session_disk_bytes = 32ull * 1024ull * 1024ull;

    jojo::Ps1OmegaInfinityControl first_control;
    const auto first = jojo::explore_ps1_omega_infinity(
        executable, root, options, first_control,
        [&](const jojo::Ps1OmegaInfinityProgress& progress) {
            if (progress.epoch >= 2u && progress.epoch_retired >= 1u) {
                first_control.request_stop();
            }
        });
    CHECK(first);
    if (!first) {
        fs::remove_all(root, ec);
        return;
    }
    CHECK(first.value.stop_reason == jojo::Ps1OmegaInfinityStopReason::user_requested);
    CHECK(first.value.epoch_count >= 2u);
    CHECK(first.value.total_retired >= 3u);
    CHECK(jojo::ps1_omega_infinity_has_resumable_session(root));
    CHECK(jojo::ps1_omega_infinity_has_compatible_resumable_session(
        root, executable.metadata.fnv1a64_hex));
    CHECK(!jojo::ps1_omega_infinity_has_compatible_resumable_session(
        root, "different-executable"));

    const auto first_total = first.value.total_retired;
    jojo::Ps1OmegaInfinityControl second_control;
    const auto second = jojo::explore_ps1_omega_infinity(
        executable, root, options, second_control,
        [&](const jojo::Ps1OmegaInfinityProgress& progress) {
            if (progress.total_retired > first_total) second_control.request_stop();
        });
    CHECK(second);
    if (second) {
        CHECK(second.value.stop_reason == jojo::Ps1OmegaInfinityStopReason::user_requested);
        CHECK(second.value.total_retired > first_total);
    }

    const auto different_executable = make_executable({
        test_mips::i(0x09u, 0u, 2u, 1u),
        test_mips::j(0x02u, 0x80010004u >> 2),
        0x00000000u,
    });
    jojo::Ps1OmegaInfinityControl mismatch_control;
    const auto mismatch = jojo::explore_ps1_omega_infinity(
        different_executable, root, options, mismatch_control);
    CHECK(mismatch);
    if (mismatch) {
        CHECK(mismatch.value.stop_reason == jojo::Ps1OmegaInfinityStopReason::invalid_resume_state);
    }

    fs::remove_all(root, ec);
}

int main() {
    test_infinity_defaults();
    test_epoch_rollover_depends_only_on_retired_limit();
    test_terminal_mmio_write_can_continue_without_device_effect();
    test_multi_epoch_stop_resume_and_identity_rejection();
    return failures ? 1 : 0;
}
