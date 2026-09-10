#include "core/ps1_exe.h"
#include "core/ps1_max3_explorer.h"
#include "core/ps1_memory_bus.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Executable make_executable(const std::vector<std::uint32_t>& words) {
    auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(parsed);
    return parsed ? std::move(parsed.value) : jojo::Ps1Executable{};
}

static jojo::Ps1Max3Options fast_max3_options() {
    jojo::Ps1Max3Options options{};
    options.max_nodes = 4u;
    options.max_branch_depth = 0u;
    options.max_total_retired = 1000u;
    options.segment_options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.segment_options.trace_capacity = 16u;
    options.segment_options.diagnostic_mmio_probe = true;
    options.segment_options.mmio_event_capacity = 32u;
    options.segment_options.bios_event_capacity = 32u;
    options.segment_options.stagnation_instruction_limit = 16u;
    return options;
}

static std::vector<std::uint32_t> cdrom_irq_then_read_istat32_program() {
    return {
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1800u),
        test_mips::i(0x09u, 0u, 9u, 0u),
        test_mips::i(0x28u, 8u, 9u, 0u),
        test_mips::i(0x09u, 0u, 9u, 1u),
        test_mips::i(0x28u, 8u, 9u, 1u),
        test_mips::i(0x0Fu, 0u, 10u, 0x1F80u),
        test_mips::i(0x0Du, 10u, 10u, 0x1070u),
        test_mips::i(0x23u, 10u, 11u, 0u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010028u >> 2),
        0x00000000u,
    };
}

static std::vector<std::uint32_t> cdrom_command_then_read_hsts_program() {
    return {
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1800u),
        test_mips::i(0x09u, 0u, 9u, 0u),
        test_mips::i(0x28u, 8u, 9u, 0u),
        test_mips::i(0x09u, 0u, 9u, 1u),
        test_mips::i(0x28u, 8u, 9u, 1u),
        test_mips::i(0x24u, 8u, 10u, 0u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010020u >> 2),
        0x00000000u,
    };
}

static std::vector<std::uint32_t> cdrom_command_bank1_then_read_result_program() {
    return {
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1800u),
        test_mips::i(0x09u, 0u, 9u, 0u),
        test_mips::i(0x28u, 8u, 9u, 0u),
        test_mips::i(0x09u, 0u, 9u, 1u),
        test_mips::i(0x28u, 8u, 9u, 1u),
        test_mips::i(0x28u, 8u, 9u, 0u),
        test_mips::i(0x24u, 8u, 10u, 1u),
        test_mips::j(0x02u, 0x80010020u >> 2),
        0x00000000u,
    };
}

static void test_istat_read32_returns_latched_irq_without_probe() {
    jojo::Ps1MemoryBus bus;
    bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    bus.set_diagnostic_mmio_probe_enabled(true);

    CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_status() == 0x0004u);

    bus.clear_last_diagnostic_mmio_probe();
    const auto istat32 = bus.read32(0x1F801070u);
    CHECK(istat32.status == jojo::R3000aBusStatus::ok);
    CHECK(istat32.value == 0x00000004u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    const auto istat16 = bus.read16(0x1F801070u);
    CHECK(istat16.status == jojo::R3000aBusStatus::ok);
    CHECK(istat16.value == 0x0004u);

    CHECK(bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read32(0x1F801070u).value == 0u);
}

static void test_hsts_read8_is_dynamic_and_not_a_probe() {
    jojo::Ps1MemoryBus bus;
    bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    bus.set_diagnostic_mmio_probe_enabled(true);

    bus.clear_last_diagnostic_mmio_probe();
    auto hsts = bus.read8(0x1F801800u);
    CHECK(hsts.status == jojo::R3000aBusStatus::ok);
    CHECK(hsts.value == 0x18u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    CHECK(bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    hsts = bus.read8(0x1F801800u);
    CHECK(hsts.status == jojo::R3000aBusStatus::ok);
    CHECK(hsts.value == 0x19u);

    CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    hsts = bus.read8(0x1F801800u);
    CHECK(hsts.status == jojo::R3000aBusStatus::ok);
    CHECK(hsts.value == 0x38u);

    const auto response = bus.read8(0x1F801801u);
    CHECK(response.status == jojo::R3000aBusStatus::ok);
    CHECK(response.value == 0x02u);
    CHECK(bus.read8(0x1F801800u).value == 0x18u);
}

static void test_result_read8_is_available_in_bank1_without_probe() {
    jojo::Ps1MemoryBus bus;
    bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    bus.set_diagnostic_mmio_probe_enabled(true);

    CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801800u).value == 0x39u);

    bus.clear_last_diagnostic_mmio_probe();
    const auto result = bus.read8(0x1F801801u);
    CHECK(result.status == jojo::R3000aBusStatus::ok);
    CHECK(result.value == 0x02u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.read8(0x1F801800u).value == 0x19u);
}

static void test_istat_read32_is_not_a_max3_dependency() {
    const auto executable = make_executable(cdrom_irq_then_read_istat32_program());
    const auto explored = jojo::explore_ps1_max3(executable, fast_max3_options());
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    CHECK(std::none_of(report.dependencies.begin(), report.dependencies.end(), [](const auto& dependency) {
        return dependency.address == 0x1F801070u &&
               dependency.width == 4u &&
               !dependency.write;
    }));
    CHECK(report.best_report.cdrom_command_count == 1u);
}

static void test_hsts_read8_is_not_a_max3_dependency() {
    const auto executable = make_executable(cdrom_command_then_read_hsts_program());
    const auto explored = jojo::explore_ps1_max3(executable, fast_max3_options());
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    CHECK(std::none_of(report.dependencies.begin(), report.dependencies.end(), [](const auto& dependency) {
        return dependency.address == 0x1F801800u &&
               dependency.width == 1u &&
               !dependency.write;
    }));
    CHECK(report.best_report.cdrom_command_count == 1u);
}

static void test_result_read8_bank1_is_not_a_max3_dependency() {
    const auto executable = make_executable(cdrom_command_bank1_then_read_result_program());
    const auto explored = jojo::explore_ps1_max3(executable, fast_max3_options());
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 1u);
    CHECK(std::none_of(report.dependencies.begin(), report.dependencies.end(), [](const auto& dependency) {
        return dependency.address == 0x1F801801u &&
               dependency.width == 1u &&
               !dependency.write;
    }));
    CHECK(report.best_report.cdrom_command_count == 1u);
}

int main() {
    jojo::Ps1MemoryBus bus;
    bus.set_diagnostic_mmio_probe_enabled(true);

    const std::array<std::uint32_t, 4> cdrom_ports{
        0x1F801800u, 0x1F801801u, 0x1F801802u, 0x1F801803u,
    };

    for (const auto address : cdrom_ports) {
        CHECK(bus.read16(address).status == jojo::R3000aBusStatus::unsupported);
        CHECK(bus.read32(address).status == jojo::R3000aBusStatus::unsupported);
        CHECK(bus.write16(address, 0x1234u).status == jojo::R3000aBusStatus::unsupported);
        CHECK(bus.write32(address, 0x12345678u).status == jojo::R3000aBusStatus::unsupported);
    }

    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.read8(0x1F801802u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write8(0x1F801802u, 0x12u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    test_istat_read32_returns_latched_irq_without_probe();
    test_hsts_read8_is_dynamic_and_not_a_probe();
    test_result_read8_is_available_in_bank1_without_probe();
    test_istat_read32_is_not_a_max3_dependency();
    test_hsts_read8_is_not_a_max3_dependency();
    test_result_read8_bank1_is_not_a_max3_dependency();
    return failures ? 1 : 0;
}