#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <iostream>
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

static void test_instruction_budget_is_explicit_stop_reason() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({10u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.instructions_retired == 10u);
    CHECK(report.presented_frames == 0u);
}

static void test_bios_entry_stops_before_executing_bios_bytes() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x800000A0u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    runtime.bus().write32(0x000000A0u, 0xFFFFFFFFu);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.last_pc == 0x800000A0u);
    CHECK(report.bios_call_count == 1u);
    CHECK(report.recent_bios_calls.size() == 1u);
    if (!report.recent_bios_calls.empty()) {
        CHECK(report.recent_bios_calls.back().table_physical == 0x000000A0u);
    }
}

static void test_mmio_access_stops_with_structured_evidence() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1070u),
        test_mips::i(0x23u, 8u, 9u, 0u),
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->guest_address == 0x1F801070u);
        CHECK(report.unsupported_access->width == 4u);
        CHECK(!report.unsupported_access->write);
    }
    CHECK(report.recent_mmio.size() == 1u);
}

int main() {
    test_instruction_budget_is_explicit_stop_reason();
    test_bios_entry_stops_before_executing_bios_bytes();
    test_mmio_access_stops_with_structured_evidence();
    return failures ? 1 : 0;
}
