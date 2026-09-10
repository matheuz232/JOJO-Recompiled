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

static std::vector<std::uint32_t> registered_irq_program() {
    return {
        test_mips::i(0x09u, 0u, 4u, 2u),                         // priority=2
        test_mips::i(0x0Fu, 0u, 5u, 0x8000u),                   // a1=0x80001000
        test_mips::i(0x0Du, 5u, 5u, 0x1000u),
        test_mips::i(0x09u, 0u, 9u, 0x02u),                     // C0:02
        test_mips::j(0x03u, 0x000000C0u >> 2),                  // jal 0x800000C0
        0x00000000u,
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1074u),                   // I_MASK
        test_mips::i(0x09u, 0u, 10u, 4u),
        test_mips::i(0x2Bu, 8u, 10u, 0u),
        test_mips::i(0x09u, 0u, 13u, 0x0401u),
        mtc0(13u, 12u),                                         // IEc + IM2
        test_mips::i(0x0Fu, 0u, 11u, 0x1F80u),
        test_mips::i(0x0Du, 11u, 11u, 0x1800u),                 // CD-ROM index port
        test_mips::i(0x09u, 0u, 12u, 0u),
        test_mips::i(0x28u, 11u, 12u, 0u),                      // bank 0
        test_mips::i(0x09u, 0u, 9u, 0x35u),                     // residual selector
        test_mips::i(0x09u, 0u, 12u, 1u),
        test_mips::i(0x28u, 11u, 12u, 1u),                      // command 01 -> IRQ2
        test_mips::j(0x02u, 0x8001004Cu >> 2),
        0x00000000u,
    };
}

static std::vector<std::uint32_t> command_program(std::uint16_t command) {
    return {
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1800u),
        test_mips::i(0x09u, 0u, 9u, 0u),
        test_mips::i(0x28u, 8u, 9u, 0u),
        test_mips::i(0x09u, 0u, 10u, command),
        test_mips::i(0x28u, 8u, 10u, 1u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    };
}

static void prepare_interrupt_node(jojo::Ps1BootRuntime& runtime,
                                   std::uint32_t first_callback) {
    CHECK(runtime.bus().write32(0x80001004u, 0u).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x80001008u, first_callback).status == jojo::R3000aBusStatus::ok);
}

static void write_return_from_exception_callback(jojo::Ps1BootRuntime& runtime,
                                                 std::uint32_t address) {
    CHECK(runtime.bus().write32(address + 0u,
        test_mips::i(0x09u, 0u, 9u, 0x17u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 4u,
        test_mips::j(0x02u, 0x000000B0u >> 2)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 8u, 0u).status == jojo::R3000aBusStatus::ok);
}

static void test_runtime_seeds_post_bios_cdrom_state() {
    auto runtime = make_runtime({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });
    CHECK(runtime.bus().cdrom().drive_status() == 0x02u);
    CHECK(runtime.bus().cdrom().interrupt_enable() == 0x1Fu);
}

static void test_enabled_cdrom_irq_enters_exception_handler_without_terminal_stop() {
    auto runtime = make_runtime(irq_program(0x0004u));
    CHECK(runtime.bus().write32(0x80000080u, test_mips::j(0x02u, 0x80000080u >> 2)).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x80000084u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    const auto report = runtime.run({32u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.interrupts_accepted == 1u);
    CHECK(runtime.bus().interrupt_status() == 0x0004u);
    CHECK((runtime.cpu_state().cop0.cause & 0x00000400u) != 0u);
    CHECK(runtime.cpu_state().cop0.epc == 0x80010030u);
    CHECK(runtime.cpu_state().pc >= 0x80000080u && runtime.cpu_state().pc < 0x80000100u);
}

static void test_masked_cdrom_irq_latches_without_preemption() {
    auto runtime = make_runtime(irq_program(0x0000u));
    const auto report = runtime.run({32u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.interrupts_accepted == 0u);
    CHECK(runtime.bus().interrupt_status() == 0x0004u);
    CHECK((runtime.cpu_state().external_interrupt_pending & 0x04u) == 0u);
}

static void test_supported_cdrom_command_reports_progress() {
    auto runtime = make_runtime(command_program(0x01u));
    jojo::Ps1BootOptions options{};
    options.instruction_budget = 32u;
    options.diagnostic_mmio_probe = true;
    options.mmio_event_capacity = 4u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.cdrom_command_count == 1u);
    CHECK(report.recent_cdrom_commands.size() == 1u);
    if (!report.recent_cdrom_commands.empty()) {
        const auto& event = report.recent_cdrom_commands.back();
        CHECK(event.command == 0x01u);
        CHECK(event.index == 0u);
        CHECK(event.status == 0x02u);
    }
    CHECK(report.speculative_mmio_count == 0u);
}

static void test_cdrom_command_count_survives_zero_event_capacity() {
    auto runtime = make_runtime(command_program(0x01u));
    jojo::Ps1BootOptions options{};
    options.instruction_budget = 32u;
    options.diagnostic_mmio_probe = true;
    options.mmio_event_capacity = 0u;
    const auto report = runtime.run(options);

    CHECK(report.cdrom_command_count == 1u);
    CHECK(report.recent_cdrom_commands.empty());
}

static void test_unsupported_cdrom_command_has_device_stop_reason() {
    auto runtime = make_runtime(command_program(0x02u));
    jojo::Ps1BootOptions options{};
    options.instruction_budget = 32u;
    options.diagnostic_mmio_probe = true;
    options.mmio_event_capacity = 4u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::device_command_unimplemented);
    CHECK(report.cdrom_command_count == 0u);
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->guest_address == 0x1F801801u);
        CHECK(report.unsupported_access->width == 1u);
        CHECK(report.unsupported_access->write);
        CHECK(report.unsupported_access->value == 0x02u);
    }
}

static void test_zero_exception_vector_dispatches_registered_handler_without_fake_a0() {
    auto runtime = make_runtime(registered_irq_program());
    prepare_interrupt_node(runtime, 0x80012000u);
    write_return_from_exception_callback(runtime, 0x80012000u);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 128u;
    options.bios_event_capacity = 64u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.interrupts_accepted >= 1u);
    bool saw_return_from_exception = false;
    for (const auto& event : report.recent_bios_calls) {
        CHECK(!(event.table == 0xA0u && event.selector == 0x35u));
        if (event.table == 0xB0u && event.selector == 0x17u) {
            saw_return_from_exception = true;
        }
    }
    CHECK(saw_return_from_exception);
}

static void test_custom_exception_vector_remains_guest_owned() {
    auto runtime = make_runtime(registered_irq_program());
    prepare_interrupt_node(runtime, 0x80012000u);
    write_return_from_exception_callback(runtime, 0x80012000u);
    CHECK(runtime.bus().write32(0x80000080u,
        test_mips::j(0x02u, 0x80000080u >> 2)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x80000084u, 0u).status == jojo::R3000aBusStatus::ok);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 96u;
    options.bios_event_capacity = 32u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.interrupts_accepted == 1u);
    for (const auto& event : report.recent_bios_calls) {
        CHECK(!(event.table == 0xB0u && event.selector == 0x17u));
    }
    CHECK(runtime.cpu_state().pc == 0x80000080u || runtime.cpu_state().pc == 0x80000084u);
}

static void test_nested_interrupt_while_callback_active_is_terminal() {
    auto runtime = make_runtime(registered_irq_program());
    prepare_interrupt_node(runtime, 0x80012100u);
    CHECK(runtime.bus().write32(0x80012100u,
        test_mips::i(0x09u, 0u, 8u, 0x0401u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x80012104u,
        mtc0(8u, 12u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x80012108u,
        test_mips::r(31u, 0u, 0u, 0u, 0x08u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x8001210Cu, 0u).status == jojo::R3000aBusStatus::ok);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 128u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::cpu_boundary);
    CHECK(report.interrupts_accepted == 2u);
    CHECK(report.cpu_diagnostic.has_value());
    if (report.cpu_diagnostic) {
        CHECK(report.cpu_diagnostic->exception_code == jojo::R3000aExceptionCode::interrupt);
        CHECK(report.cpu_diagnostic->pc == 0x80012108u);
    }
}

int main() {
    test_runtime_seeds_post_bios_cdrom_state();
    test_enabled_cdrom_irq_enters_exception_handler_without_terminal_stop();
    test_masked_cdrom_irq_latches_without_preemption();
    test_supported_cdrom_command_reports_progress();
    test_cdrom_command_count_survives_zero_event_capacity();
    test_unsupported_cdrom_command_has_device_stop_reason();
    test_zero_exception_vector_dispatches_registered_handler_without_fake_a0();
    test_custom_exception_vector_remains_guest_owned();
    test_nested_interrupt_while_callback_active_is_terminal();
    return failures ? 1 : 0;
}
