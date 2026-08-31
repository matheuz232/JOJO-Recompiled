#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static constexpr std::uint32_t kPriority = 2u;
static constexpr std::uint32_t kHead = 0x80098c00u;
static constexpr std::uint32_t kTarget = 0x80098d88u;
static constexpr std::uint32_t kTail = 0x80098e80u;

static std::uint32_t priority_entry(jojo::PsxRuntime& runtime) {
    const auto base = jojo::psx_bus_read_u32(runtime.bus, 0x00000100u);
    CHECK(base.reason == jojo::PsxBusAccessReason::ok);
    return base.value + kPriority * 8u;
}

static void prepare_runtime(jojo::PsxRuntime& runtime) {
    CHECK(jojo::materialize_scph1001_exception_control_blocks(runtime));
}

static void call_sysdeq(jojo::PsxRuntime& runtime, std::uint32_t requested,
                        std::uint32_t ra = 0x8004e0bcu) {
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000c0u);
    runtime.cpu.gpr[4] = kPriority;
    runtime.cpu.gpr[5] = requested;
    runtime.cpu.gpr[9] = 0x03u;
    runtime.cpu.gpr[31] = ra;
    runtime.cpu.gpr[2] = 0xfeedfaceu;
}

static void test_empty_chain_returns_null() {
    jojo::PsxRuntime runtime{};
    prepare_runtime(runtime);
    const auto entry = priority_entry(runtime);

    call_sysdeq(runtime, kTarget);
    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.cpu.pc == 0x8004e0bcu);
    CHECK(runtime.cpu.next_pc == 0x8004e0c0u);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == 0u);
}

static void test_head_removal_returns_removed_pointer() {
    jojo::PsxRuntime runtime{};
    prepare_runtime(runtime);
    const auto entry = priority_entry(runtime);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, entry, kTarget) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kTarget, kTail) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kTail, 0u) == jojo::PsxBusAccessReason::ok);

    call_sysdeq(runtime, kTarget);
    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == kTarget);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == kTail);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kTarget).value == kTail);
}

static void test_deep_removal_matches_scph1001_routine_1444() {
    jojo::PsxRuntime runtime{};
    prepare_runtime(runtime);
    const auto entry = priority_entry(runtime);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, entry, kHead) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kHead, kTarget) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kTarget, kTail) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kTail, 0u) == jojo::PsxBusAccessReason::ok);

    call_sysdeq(runtime, kTarget);
    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == kTarget);
    CHECK(runtime.cpu.pc == 0x8004e0bcu);
    CHECK(runtime.cpu.next_pc == 0x8004e0c0u);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == kHead);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kHead).value == kTail);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kTarget).value == kTail);
}

static void test_missing_node_returns_null_and_preserves_chain() {
    jojo::PsxRuntime runtime{};
    prepare_runtime(runtime);
    const auto entry = priority_entry(runtime);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, entry, kHead) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kHead, kTail) == jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kTail, 0u) == jojo::PsxBusAccessReason::ok);

    call_sysdeq(runtime, kTarget);
    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == kHead);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kHead).value == kTail);
}

int main() {
    test_empty_chain_returns_null();
    test_head_removal_returns_removed_pointer();
    test_deep_removal_matches_scph1001_routine_1444();
    test_missing_node_returns_null_and_preserves_chain();
    if (failures) return 1;
    std::cout << "SCPH-1001 SysDeqIntRP assertions passed\n";
    return 0;
}
