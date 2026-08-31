#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static constexpr std::uint32_t kPriority = 2u;
static constexpr std::uint32_t kNewNode = 0x80098d88u;
static constexpr std::uint32_t kOldHead = 0x80098c00u;
static constexpr std::uint32_t kRa = 0x8004e0c8u;

static std::uint32_t priority_entry(jojo::PsxRuntime& runtime) {
    const auto base = jojo::psx_bus_read_u32(runtime.bus, 0x00000100u);
    CHECK(base.reason == jojo::PsxBusAccessReason::ok);
    return base.value + kPriority * 8u;
}

static void call_sysenq(jojo::PsxRuntime& runtime) {
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000c0u);
    runtime.cpu.gpr[4] = kPriority;
    runtime.cpu.gpr[5] = kNewNode;
    runtime.cpu.gpr[9] = 0x02u;
    runtime.cpu.gpr[31] = kRa;
    runtime.cpu.gpr[2] = 0xfeedfaceu;
}

static void test_empty_chain_inserts_new_head_and_returns_zero() {
    jojo::PsxRuntime runtime{};
    CHECK(jojo::materialize_scph1001_exception_control_blocks(runtime));
    const auto entry = priority_entry(runtime);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == 0u);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kNewNode, 0xdeadbeefu) ==
          jojo::PsxBusAccessReason::ok);

    call_sysenq(runtime);
    const auto result = jojo::step_psx_runtime(runtime);

    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.cpu.pc == kRa);
    CHECK(runtime.cpu.next_pc == kRa + 4u);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == kNewNode);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kNewNode).value == 0u);
}

static void test_existing_chain_pushes_new_node_at_front() {
    jojo::PsxRuntime runtime{};
    CHECK(jojo::materialize_scph1001_exception_control_blocks(runtime));
    const auto entry = priority_entry(runtime);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, entry, kOldHead) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kOldHead, 0u) ==
          jojo::PsxBusAccessReason::ok);
    CHECK(jojo::psx_bus_write_u32(runtime.bus, kNewNode, 0xdeadbeefu) ==
          jojo::PsxBusAccessReason::ok);

    call_sysenq(runtime);
    const auto result = jojo::step_psx_runtime(runtime);

    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.cpu.pc == kRa);
    CHECK(runtime.cpu.next_pc == kRa + 4u);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, entry).value == kNewNode);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kNewNode).value == kOldHead);
    CHECK(jojo::psx_bus_read_u32(runtime.bus, kOldHead).value == 0u);
}

int main() {
    test_empty_chain_inserts_new_head_and_returns_zero();
    test_existing_chain_pushes_new_node_at_front();
    if (failures) return 1;
    std::cout << "SCPH-1001 SysEnqIntRP assertions passed\n";
    return 0;
}
