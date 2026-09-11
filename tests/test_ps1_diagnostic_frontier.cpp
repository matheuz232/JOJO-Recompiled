#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1BootRuntime make_runtime(const std::vector<std::uint32_t>& words) {
    auto executable = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(executable);
    auto runtime = executable ? jojo::Ps1BootRuntime::create(executable.value)
                              : jojo::Result<jojo::Ps1BootRuntime>::failure(
                                    jojo::ErrorCode::invalid_installation,
                                    "synthetic executable parse failed");
    CHECK(runtime);
    return runtime ? std::move(runtime.value) : jojo::Ps1BootRuntime{};
}

static jojo::Ps1BootRuntime make_unknown_bios_runtime() {
    return make_runtime({
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x5678u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    });
}

static constexpr std::uint32_t mtc0(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) | (0x04u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static constexpr std::uint32_t ctc2(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x12u << 26) | (0x06u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static void test_unknown_bios_frontier_can_branch_from_snapshot() {
    auto stopped = make_unknown_bios_runtime();
    CHECK(!stopped.apply_diagnostic_bios_fallback(jojo::Ps1BiosFallback::return_zero));

    const auto frontier = stopped.run({16u});
    CHECK(frontier.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(frontier.last_pc == 0x000000A0u);
    CHECK(stopped.cpu_state().gpr[2] == 0x00001234u);

    const std::array<jojo::Ps1BiosFallback, 4> policies{
        jojo::Ps1BiosFallback::return_zero,
        jojo::Ps1BiosFallback::return_one,
        jojo::Ps1BiosFallback::return_minus_one,
        jojo::Ps1BiosFallback::preserve_v0,
    };
    const std::array<std::uint32_t, 4> expected{
        0u, 1u, 0xFFFFFFFFu, 0x00001234u,
    };

    for (std::size_t i = 0; i < policies.size(); ++i) {
        auto child = stopped;
        CHECK(child.apply_diagnostic_bios_fallback(policies[i]));
        CHECK(child.cpu_state().gpr[2] == expected[i]);
        CHECK(!child.apply_diagnostic_bios_fallback(policies[i]));
        const auto resumed = child.run({2u});
        CHECK(resumed.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(resumed.instructions_retired == 2u);
        CHECK(child.cpu_state().gpr[16] == 0x00005678u);
    }
}

static void test_stagnation_watchdog_stops_tight_loop() {
    auto runtime = make_runtime({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });
    jojo::Ps1BootOptions options{};
    options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.trace_capacity = 8u;
    options.stagnation_instruction_limit = 64u;
    const auto report = runtime.run(options);
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::diagnostic_stall);
    CHECK(report.instructions_retired == 64u);
    CHECK(report.recent_trace.size() == 8u);
}

static void test_diagnostic_state_fingerprint_tracks_guest_state() {
    const std::vector<std::uint32_t> loop{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto baseline = make_runtime(loop);
    auto identical = make_runtime(loop);
    CHECK(baseline.diagnostic_state_hash() == identical.diagnostic_state_hash());

    auto ram_changed = baseline;
    CHECK(ram_changed.bus().write32(0x00000200u, 0x12345678u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(ram_changed.diagnostic_state_hash() != baseline.diagnostic_state_hash());

    auto dicr_changed = baseline;
    CHECK(dicr_changed.bus().write32(0x1F8010F4u, 0x00000001u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(dicr_changed.diagnostic_state_hash() != baseline.diagnostic_state_hash());

    auto shadow_changed = baseline;
    shadow_changed.bus().set_diagnostic_mmio_probe_enabled(true);
    CHECK(shadow_changed.bus().write32(0x1F801080u, 0xA5A55A5Au).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(shadow_changed.diagnostic_state_hash() != baseline.diagnostic_state_hash());

    auto frontier = make_unknown_bios_runtime();
    CHECK(frontier.run({16u}).stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    auto zero = frontier;
    auto one = frontier;
    CHECK(zero.apply_diagnostic_bios_fallback(jojo::Ps1BiosFallback::return_zero));
    CHECK(one.apply_diagnostic_bios_fallback(jojo::Ps1BiosFallback::return_one));
    CHECK(zero.diagnostic_state_hash() != one.diagnostic_state_hash());
}

static void test_diagnostic_state_fingerprint_tracks_only_gte_control_delta() {
    auto prepared = make_runtime({
        0x3C084000u,
        mtc0(8u, 12u),
        0x24080155u,
        0x00000000u,
        0x00000000u,
    });

    CHECK(prepared.run({3u}).stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    auto baseline = prepared;
    auto mutated = prepared;
    CHECK(baseline.diagnostic_state_hash() == mutated.diagnostic_state_hash());

    constexpr std::uint32_t kPatchAddress = 0x8001000Cu;
    constexpr std::uint32_t kObservedCtc2Zsf3 = 0x48C8E800u;
    static_assert(kObservedCtc2Zsf3 == ctc2(8u, 29u));

    CHECK(mutated.bus().write32(kPatchAddress, kObservedCtc2Zsf3).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(mutated.run({1u}).stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(mutated.cpu_state().cop2_gte.control[29] == 0x00000155u);
    CHECK(mutated.bus().write32(kPatchAddress, 0x00000000u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(baseline.run({1u}).stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(baseline.cpu_state().pc == mutated.cpu_state().pc);
    CHECK(baseline.cpu_state().gpr == mutated.cpu_state().gpr);
    CHECK(baseline.cpu_state().cop0.status == mutated.cpu_state().cop0.status);
    CHECK(baseline.bus().read32(kPatchAddress).value == mutated.bus().read32(kPatchAddress).value);

    CHECK(mutated.diagnostic_state_hash() != baseline.diagnostic_state_hash());
}

static void test_observed_commercial_ctc2_sequence_retires_past_frontier() {
    constexpr std::uint32_t kObservedCtc2Zsf3 = 0x48C8E800u;
    static_assert(kObservedCtc2Zsf3 == ctc2(8u, 29u));

    auto runtime = make_runtime({
        0x3C084000u,
        mtc0(8u, 12u),
        0x24080155u,
        kObservedCtc2Zsf3,
        0x24101234u,
    });

    const auto report = runtime.run({5u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.instructions_retired == 5u);
    CHECK(runtime.cpu_state().cop2_gte.control[29] == 0x00000155u);
    CHECK(runtime.cpu_state().gpr[16] == 0x00001234u);
    CHECK(runtime.cpu_state().pc == 0x80010014u);
}

static void test_sys00_nofunction_continues_without_clobbering_v0() {
    auto runtime = make_runtime({
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 4u, 0x0000u),
        0x0000000Cu,
        test_mips::i(0x09u, 0u, 16u, 0x5678u),
        test_mips::j(0x02u, 0x80010010u >> 2),
        0x00000000u,
    });

    const auto report = runtime.run({12u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.cpu_state().gpr[2] == 0x00001234u);
    CHECK(runtime.cpu_state().gpr[16] == 0x00005678u);
}

static void test_sys01_entercriticalsection_disables_interrupts_and_returns_prior_state() {
    auto runtime = make_runtime({
        test_mips::i(0x09u, 0u, 8u, 0x0401u),
        mtc0(8u, 12u),
        test_mips::i(0x09u, 0u, 4u, 0x0001u),
        0x0000000Cu,
        test_mips::j(0x02u, 0x80010010u >> 2),
        0x00000000u,
    });

    const auto report = runtime.run({12u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.cpu_state().gpr[2] == 1u);
    CHECK((runtime.cpu_state().cop0.status & 0x00000401u) == 0u);
}

static void test_sys02_exitcriticalsection_enables_interrupts_and_preserves_v0() {
    auto runtime = make_runtime({
        test_mips::i(0x09u, 0u, 8u, 0x0000u),
        mtc0(8u, 12u),
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 4u, 0x0002u),
        0x0000000Cu,
        test_mips::j(0x02u, 0x80010014u >> 2),
        0x00000000u,
    });

    const auto report = runtime.run({12u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.cpu_state().gpr[2] == 0x00001234u);
    CHECK((runtime.cpu_state().cop0.status & 0x00000401u) == 0x00000401u);
}

static void test_sys03_remains_a_cpu_boundary_until_threads_are_modeled() {
    auto runtime = make_runtime({
        test_mips::i(0x09u, 0u, 4u, 0x0003u),
        0x0000000Cu,
        test_mips::j(0x02u, 0x80010008u >> 2),
        0x00000000u,
    });

    const auto report = runtime.run({12u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::cpu_boundary);
    CHECK(report.cpu_diagnostic.has_value());
    if (report.cpu_diagnostic) {
        CHECK(report.cpu_diagnostic->exception_code == jojo::R3000aExceptionCode::syscall);
        CHECK(report.cpu_diagnostic->pc == 0x80010004u);
    }
}

static jojo::Ps1BootRuntime make_mmio_load_runtime(std::uint8_t op,
                                                   std::uint32_t address) {
    return make_runtime({
        test_mips::i(0x0Fu, 0u, 8u, static_cast<std::uint16_t>(address >> 16u)),
        test_mips::i(0x0Du, 8u, 8u, static_cast<std::uint16_t>(address)),
        test_mips::i(0x09u, 0u, 9u, 0x1234u),
        test_mips::i(op, 8u, 9u, 0u),
        test_mips::r(9u, 0u, 16u, 0u, 0x21u),
        test_mips::r(9u, 0u, 17u, 0u, 0x21u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    });
}

static void check_mmio_load_fallback(std::uint8_t op,
                                     std::uint32_t address,
                                     std::uint8_t width,
                                     std::uint32_t fallback,
                                     std::uint32_t expected) {
    auto runtime = make_mmio_load_runtime(op, address);
    const auto stopped = runtime.run({16u});
    CHECK(stopped.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(stopped.instructions_retired == 3u);
    CHECK(stopped.last_pc == 0x8001000Cu);
    CHECK(runtime.cpu_state().pc == 0x8001000Cu);
    CHECK(runtime.cpu_state().gpr[9] == 0x00001234u);
    CHECK(!runtime.cpu_state().pending_load.valid);
    CHECK(runtime.diagnostic_mmio_read_frontier().has_value());
    if (runtime.diagnostic_mmio_read_frontier()) {
        const auto& frontier = *runtime.diagnostic_mmio_read_frontier();
        CHECK(frontier.pc == 0x8001000Cu);
        CHECK(frontier.opcode == test_mips::i(op, 8u, 9u, 0u));
        CHECK(frontier.access.physical_address == address);
        CHECK(frontier.access.width == width);
        CHECK(!frontier.access.write);
    }

    CHECK(runtime.apply_diagnostic_mmio_read_fallback(fallback));
    CHECK(!runtime.apply_diagnostic_mmio_read_fallback(fallback));
    const auto resumed = runtime.run({3u});
    CHECK(resumed.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(resumed.instructions_retired == 3u);
    CHECK(runtime.cpu_state().gpr[16] == 0x00001234u);
    CHECK(runtime.cpu_state().gpr[17] == expected);
}

static void test_mmio_read_frontier_reexecutes_normal_load_semantics() {
    check_mmio_load_fallback(0x20u, 0x1F801802u, 1u, 0x80u, 0xFFFFFF80u);
    check_mmio_load_fallback(0x24u, 0x1F801802u, 1u, 0x80u, 0x00000080u);
    check_mmio_load_fallback(0x21u, 0x1F801800u, 2u, 0x8001u, 0xFFFF8001u);
    check_mmio_load_fallback(0x25u, 0x1F801800u, 2u, 0x8001u, 0x00008001u);
    check_mmio_load_fallback(0x23u, 0x1F801800u, 4u, 0x89ABCDEFu, 0x89ABCDEFu);
}

static void test_mmio_read_frontier_hash_tracks_armed_fallback_value() {
    auto stopped = make_mmio_load_runtime(0x24u, 0x1F801802u);
    CHECK(stopped.run({16u}).stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    auto zero = stopped;
    auto one = stopped;
    CHECK(zero.apply_diagnostic_mmio_read_fallback(0u));
    CHECK(one.apply_diagnostic_mmio_read_fallback(1u));
    CHECK(zero.diagnostic_state_hash() != one.diagnostic_state_hash());
}

static void test_mmio_read_frontier_rejects_non_read_boundaries() {
    auto write_runtime = make_runtime({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1802u),
        test_mips::i(0x09u, 0u, 9u, 0x0055u),
        test_mips::i(0x28u, 8u, 9u, 0u),
    });
    CHECK(write_runtime.run({16u}).stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(!write_runtime.diagnostic_mmio_read_frontier().has_value());
    CHECK(!write_runtime.apply_diagnostic_mmio_read_fallback(0u));

    auto cpu_runtime = make_runtime({
        test_mips::i(0x09u, 0u, 4u, 0x0003u),
        0x0000000Cu,
    });
    CHECK(cpu_runtime.run({16u}).stop_reason == jojo::Ps1BootStopReason::cpu_boundary);
    CHECK(!cpu_runtime.diagnostic_mmio_read_frontier().has_value());
    CHECK(!cpu_runtime.apply_diagnostic_mmio_read_fallback(0u));

    auto bios_runtime = make_unknown_bios_runtime();
    CHECK(bios_runtime.run({16u}).stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(!bios_runtime.diagnostic_mmio_read_frontier().has_value());
    CHECK(!bios_runtime.apply_diagnostic_mmio_read_fallback(0u));
}

int main() {
    test_unknown_bios_frontier_can_branch_from_snapshot();
    test_stagnation_watchdog_stops_tight_loop();
    test_diagnostic_state_fingerprint_tracks_guest_state();
    test_diagnostic_state_fingerprint_tracks_only_gte_control_delta();
    test_observed_commercial_ctc2_sequence_retires_past_frontier();
    test_sys00_nofunction_continues_without_clobbering_v0();
    test_sys01_entercriticalsection_disables_interrupts_and_returns_prior_state();
    test_sys02_exitcriticalsection_enables_interrupts_and_preserves_v0();
    test_sys03_remains_a_cpu_boundary_until_threads_are_modeled();
    test_mmio_read_frontier_reexecutes_normal_load_semantics();
    test_mmio_read_frontier_hash_tracks_armed_fallback_value();
    test_mmio_read_frontier_rejects_non_read_boundaries();
    return failures ? 1 : 0;
}
