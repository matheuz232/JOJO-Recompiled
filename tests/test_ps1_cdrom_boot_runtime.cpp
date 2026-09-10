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

constexpr std::uint32_t mtc0(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) | (0x04u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static std::vector<std::uint32_t> irq_program(std::uint16_t interrupt_mask) {
    return {
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1074u),
        test_mips::i(0x09u, 0u, 9u, interrupt_mask),
        test_mips::i(0x2Bu, 8u, 9u, 0u),
        test_mips::i(0x09u, 0u, 13u, 0x0401u),
        mtc0(13u, 12u),
        test_mips::i(0x0Fu, 0u, 10u, 0x1F80u),
        test_mips::i(0x0Du, 10u, 10u, 0x1800u),
        test_mips::i(0x09u, 0u, 11u, 0u),
        test_mips::i(0x28u, 10u, 11u, 0u),
        test_mips::i(0x09u, 0u, 12u, 1u),
        test_mips::i(0x28u, 10u, 12u, 1u),
        test_mips::j(0x02u, 0x80010030u >> 2),
        0x00000000u,
    };
}

static void test_runtime_seeds_post_bios_cdrom_state() {
    auto runtime = make_runtime({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });
    CHECK(runtime.bus().cdrom().drive_status() == 0x02u);
    CHECK(runtime.bus().cdrom().interrupt_enable() == 0x1Fu);
}

static void test_enabled_cdrom_irq_reaches_r3000a_ip2() {
    auto runtime = make_runtime(irq_program(0x0004u));
    const auto report = runtime.run({32u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::cpu_boundary);
    CHECK(report.interrupts_accepted == 1u);
    CHECK(runtime.bus().interrupt_status() == 0x0004u);
    CHECK((runtime.cpu_state().cop0.cause & 0x00000400u) != 0u);
}

static void test_masked_cdrom_irq_latches_without_preemption() {
    auto runtime = make_runtime(irq_program(0x0000u));
    const auto report = runtime.run({32u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.interrupts_accepted == 0u);
    CHECK(runtime.bus().interrupt_status() == 0x0004u);
    CHECK((runtime.cpu_state().external_interrupt_pending & 0x04u) == 0u);
}

int main() {
    test_runtime_seeds_post_bios_cdrom_state();
    test_enabled_cdrom_irq_reaches_r3000a_ip2();
    test_masked_cdrom_irq_latches_without_preemption();
    return failures ? 1 : 0;
}
