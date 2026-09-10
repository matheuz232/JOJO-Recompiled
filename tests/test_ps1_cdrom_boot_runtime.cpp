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
        test_mips::i(0x09u, 0u, 4u, 2u),
        test_mips::i(0x0Fu, 0u, 5u, 0x8000u),
        test_mips::i(0x0Du, 5u, 5u, 0x1000u),
        test_mips::i(0x09u, 0u, 9u, 0x02u),
        test_mips::j(0x03u, 0x000000C0u >> 2),
        0x00000000u,
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1074u),
        test_mips::i(0x09u, 0u, 10u, 4u),
        test_mips::i(0x2Bu, 8u, 10u, 0u),
        test_mips::i(0x09u, 0u, 13u, 0x0401u),
        mtc0(13u, 12u),
        test_mips::i(0x0Fu, 0u, 11u, 0x1F80u),
        test_mips::i(0x0Du, 11u, 11u, 0x1800u),
        test_mips::i(0x09u, 0u, 12u, 0u),
        test_mips::i(0x28u, 11u, 12u, 0u),
        test_mips::i(0x09u, 0u, 9u, 0x35u),
        test_mips::i(0x09u, 0u, 12u, 1u),
        test_mips::i(0x28u, 11u, 12u, 1u),
        test_mips::j(0x02u, 0x8001004Cu >> 2),
        0x00000000u,
    };
}

static std::vector<std::uint32_t> hook_pending_load_program() {
    return {
        test_mips::i(0x0Fu, 0u, 4u, 0x8000u),
        test_mips::i(0x0Du, 4u, 4u, 0x3000u),
        test_mips::i(0x09u, 0u, 9u, 0x19u),
        test_mips::j(0x03u, 0x000000B0u >> 2),
        0x00000000u,
        test_mips::i(0x09u, 0u, 13u, 0x0401u),
        mtc0(13u, 12u),
        test_mips::i(0x0Fu, 0u, 8u, 0x8001u),
        test_mips::i(0x23u, 8u, 14u, 0x0200u),
        test_mips::j(0x02u, 0x80010024u >> 2),
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

static void write_hook_buffer(jojo::Ps1BootRuntime& runtime,
                              std::uint32_t hook,
                              std::uint32_t target) {
    CHECK(runtime.bus().write32(hook + 0x00u, target).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(hook + 0x04u, 0x801FF000u).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(hook + 0x08u, 0x80003F00u).status == jojo::R3000aBusStatus::ok);
    for (std::uint32_t i = 0u; i < 8u; ++i) {
        CHECK(runtime.bus().write32(hook + 0x0Cu + i * 4u, 0x16000000u + i).status ==
              jojo::R3000aBusStatus::ok);
    }
    CHECK(runtime.bus().write32(hook + 0x2Cu, 0x80004000u).status == jojo::R3000aBusStatus::ok);
}

static void write_ack_and_return_hook(jojo::Ps1BootRuntime& runtime,
                                      std::uint32_t address) {
    CHECK(runtime.bus().write32(address + 0x00u,
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 0x04u,
        test_mips::i(0x0Du, 8u, 8u, 0x1070u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 0x08u,
        test_mips::i(0x2Bu, 8u, 0u, 0u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 0x0Cu,
        test_mips::i(0x09u, 0u, 9u, 0x17u)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 0x10u,
        test_mips::j(0x02u, 0x000000B0u >> 2)).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(address + 0x14u, 0u).status == jojo::R3000aBusStatus::ok);
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
        CHECK(!(event.table_physical == 0xA0u && event.selector == 0x35u));
        if (event.table_physical == 0xB0u && event.selector == 0x17u) {
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
        CHECK(!(event.table_physical == 0xB0u && event.selector == 0x17u));
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

static void test_custom_hook_guest_can_ack_irq_and_return_with_pending_load_retired() {
    auto runtime = make_runtime(hook_pending_load_program());
    constexpr std::uint32_t hook = 0x80003000u;
    constexpr std::uint32_t hook_target = 0x80014000u;
    write_hook_buffer(runtime, hook, hook_target);
    write_ack_and_return_hook(runtime, hook_target);
    CHECK(runtime.bus().write32(0x80010200u, 0xCAFEBABEu).status == jojo::R3000aBusStatus::ok);

    jojo::Ps1BootOptions setup{};
    setup.instruction_budget = 9u;
    setup.bios_event_capacity = 16u;
    const auto before_irq = runtime.run(setup);
    CHECK(before_irq.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.cpu_state().pending_load.valid);
    CHECK(runtime.cpu_state().gpr[14] != 0xCAFEBABEu);
    CHECK(runtime.bios_interrupt_hook_address().has_value());
    if (runtime.bios_interrupt_hook_address()) CHECK(*runtime.bios_interrupt_hook_address() == hook);

    CHECK(runtime.bus().write32(0x1F801074u, 0x00000004u).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 16u;
    options.bios_event_capacity = 32u;
    const auto report = runtime.run(options);
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.interrupts_accepted == 1u);
    CHECK(runtime.cpu_state().gpr[14] == 0xCAFEBABEu);
    CHECK(!runtime.cpu_state().pending_load.valid);
    CHECK(runtime.bus().interrupt_status() == 0u);
    bool saw_return_from_exception = false;
    for (const auto& event : report.recent_bios_calls) {
        if (event.table_physical == 0xB0u && event.selector == 0x17u) {
            saw_return_from_exception = true;
        }
    }
    CHECK(saw_return_from_exception);
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
    test_custom_hook_guest_can_ack_irq_and_return_with_pending_load_retired();
    return failures ? 1 : 0;
}
