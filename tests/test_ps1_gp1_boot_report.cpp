#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <iostream>
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

static void test_two_real_gp1_commands_are_reported_without_speculation() {
    auto runtime = make_runtime({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),          // lui t0,1f80
        test_mips::i(0x0Du, 8u, 8u, 0x1814u),          // ori t0,t0,1814
        test_mips::i(0x0Fu, 0u, 9u, 0x0300u),          // lui t1,0300 (GP1 display enable)
        test_mips::i(0x2Bu, 8u, 9u, 0u),               // sw t1,0(t0)
        test_mips::i(0x0Fu, 0u, 9u, 0x0400u),          // lui t1,0400
        test_mips::i(0x0Du, 9u, 9u, 0x0002u),          // ori t1,t1,2 (DMA CPU->GP0)
        test_mips::i(0x2Bu, 8u, 9u, 0u),               // sw t1,0(t0)
        test_mips::j(0x02u, 0x8001001Cu >> 2),
        0x00000000u,
    });

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 12u;
    options.trace_capacity = 16u;
    options.diagnostic_mmio_probe = true;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.gpu_gp1_command_count == 2u);
    CHECK(report.speculative_mmio_count == 0u);
    CHECK(report.presented_frames == 0u);
    CHECK(report.vram_write_count == 0u);
    CHECK(runtime.bus().gpu().gp1_command_count() == 2u);
}

int main() {
    test_two_real_gp1_commands_are_reported_without_speculation();
    return failures ? 1 : 0;
}
