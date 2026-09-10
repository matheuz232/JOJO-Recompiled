#include "core/ps1_interrupt_continuation.h"
#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
    << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void enqueue_node(jojo::Ps1HleBios& bios,
                         jojo::Ps1MemoryBus& bus,
                         std::uint32_t priority,
                         std::uint32_t node,
                         std::uint32_t second,
                         std::uint32_t first) {
    CHECK(bus.write32(node + 4u, second).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write32(node + 8u, first).status == jojo::R3000aBusStatus::ok);
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000C0u;
    cpu.next_pc = 0x000000C4u;
    cpu.gpr[4] = priority;
    cpu.gpr[5] = node;
    cpu.gpr[31] = 0x8001F000u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::c0, 0x02u, cpu.pc,
        cpu.gpr[4], cpu.gpr[5], cpu.gpr[6], cpu.gpr[7], cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
}

static jojo::R3000aState interrupted_cpu() {
    jojo::R3000aState cpu{};
    cpu.pc = 0x80000080u;
    cpu.next_pc = 0x80000084u;
    cpu.cop0.status = 0x00000404u;
    cpu.cop0.epc = 0x80010000u;
    return cpu;
}

static void test_begin_and_restore_exact_v0_context() {
    jojo::R3000aState cpu{};
    for (std::uint32_t i = 1; i < 32; ++i) cpu.gpr[i] = 0x10000000u + i;
    cpu.hi = 0x11112222u;
    cpu.lo = 0x33334444u;
    cpu.cop0.status = 0x00000404u;
    cpu.cop0.cause = 0x00000400u;
    cpu.cop0.epc = 0x80012000u;
    cpu.cop2_gte.control[0] = 0xABCDEF01u;
    cpu.external_interrupt_pending = 0x04u;

    jojo::Ps1InterruptContinuation continuation;
    const auto inactive_hash = continuation.diagnostic_state_hash();
    continuation.begin(cpu, 0x00000401u, 0x80012000u, 0x80012004u);
    CHECK(continuation.active());
    CHECK(continuation.phase() == jojo::Ps1InterruptContinuationPhase::dispatch);
    CHECK(continuation.diagnostic_state_hash() != inactive_hash);

    cpu.gpr[5] = 0xDEADBEEFu;
    cpu.hi = 1u;
    cpu.lo = 2u;
    cpu.cop0.status = 0u;
    cpu.cop0.cause = 0x12340400u;
    cpu.cop0.epc = 0x87654321u;
    cpu.cop2_gte.control[0] = 0x10203040u;
    cpu.external_interrupt_pending = 0x08u;
    cpu.pending_load = {true, 7u, 0xCAFEBABEu};
    cpu.delay_slot.active = true;

    continuation.return_from_exception(cpu);
    CHECK(!continuation.active());
    CHECK(cpu.gpr[5] == 0x10000005u);
    CHECK(cpu.hi == 0x11112222u && cpu.lo == 0x33334444u);
    CHECK(cpu.cop0.status == 0x00000401u);
    CHECK(cpu.pc == 0x80012000u && cpu.next_pc == 0x80012004u);
    CHECK(!cpu.pending_load.valid && !cpu.delay_slot.active);
    CHECK(cpu.cop0.cause == 0x12340400u);
    CHECK(cpu.cop0.epc == 0x87654321u);
    CHECK(cpu.cop2_gte.control[0] == 0x10203040u);
    CHECK(cpu.external_interrupt_pending == 0x08u);
    CHECK(continuation.diagnostic_state_hash() == inactive_hash);
}

static void test_equal_continuations_hash_equal() {
    jojo::R3000aState cpu{};
    cpu.gpr[3] = 0x1234u;
    jojo::Ps1InterruptContinuation a;
    jojo::Ps1InterruptContinuation b;
    a.begin(cpu, 0x401u, 0x80010000u, 0x80010004u);
    b.begin(cpu, 0x401u, 0x80010000u, 0x80010004u);
    CHECK(a.diagnostic_state_hash() == b.diagnostic_state_hash());
}

static void test_first_zero_skips_second_and_restores() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    enqueue_node(bios, bus, 0u, 0x80001000u, 0x80012100u, 0x80012000u);

    auto cpu = interrupted_cpu();
    jojo::Ps1InterruptContinuation continuation;
    continuation.begin(cpu, 0x00000401u, 0x80010000u, 0x80010004u);

    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::guest_execution);
    CHECK(cpu.pc == 0x80012000u);
    CHECK(cpu.gpr[31] == jojo::Ps1InterruptContinuation::callback_return_sentinel);

    cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
    cpu.gpr[2] = 0u;
    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::restored);
    CHECK(!continuation.active());
}

static void test_first_nonzero_runs_second_before_restoring() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    enqueue_node(bios, bus, 0u, 0x80001000u, 0x80012100u, 0x80012000u);

    auto cpu = interrupted_cpu();
    jojo::Ps1InterruptContinuation continuation;
    continuation.begin(cpu, 0x00000401u, 0x80010000u, 0x80010004u);
    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::guest_execution);

    cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
    cpu.gpr[2] = 1u;
    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::guest_execution);
    CHECK(cpu.pc == 0x80012100u);
    CHECK(continuation.phase() == jojo::Ps1InterruptContinuationPhase::second_callback);

    cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::restored);
}

static void test_linked_nodes_then_priorities_run_in_order() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    enqueue_node(bios, bus, 0u, 0x80001020u, 0u, 0x80012040u);
    enqueue_node(bios, bus, 0u, 0x80001000u, 0u, 0x80012000u);
    enqueue_node(bios, bus, 1u, 0x80001100u, 0u, 0x80013000u);
    enqueue_node(bios, bus, 2u, 0x80001200u, 0u, 0x80014000u);
    enqueue_node(bios, bus, 3u, 0x80001300u, 0u, 0x80015000u);

    auto cpu = interrupted_cpu();
    jojo::Ps1InterruptContinuation continuation;
    continuation.begin(cpu, 0x00000401u, 0x80010000u, 0x80010004u);

    constexpr std::array<std::uint32_t, 5> expected{
        0x80012000u, 0x80012040u, 0x80013000u, 0x80014000u, 0x80015000u};
    auto result = continuation.drive(cpu, bus, bios);
    CHECK(result.status == jojo::Ps1InterruptDriveStatus::guest_execution);
    for (std::size_t i = 0u; i < expected.size(); ++i) {
        CHECK(cpu.pc == expected[i]);
        cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
        cpu.gpr[2] = 0u;
        result = continuation.drive(cpu, bus, bios);
        if (i + 1u < expected.size()) {
            CHECK(result.status == jojo::Ps1InterruptDriveStatus::guest_execution);
        } else {
            CHECK(result.status == jojo::Ps1InterruptDriveStatus::restored);
        }
    }
}

static void test_cycle_is_terminal() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    constexpr std::uint32_t node = 0x80001000u;
    enqueue_node(bios, bus, 0u, node, 0u, 0x80012000u);
    CHECK(bus.write32(node, node).status == jojo::R3000aBusStatus::ok);

    auto cpu = interrupted_cpu();
    jojo::Ps1InterruptContinuation continuation;
    continuation.begin(cpu, 0x00000401u, 0x80010000u, 0x80010004u);
    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::guest_execution);
    cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
    cpu.gpr[2] = 0u;
    CHECK(continuation.drive(cpu, bus, bios).status ==
          jojo::Ps1InterruptDriveStatus::terminal);
    CHECK(continuation.active());
}

int main() {
    test_begin_and_restore_exact_v0_context();
    test_equal_continuations_hash_equal();
    test_first_zero_skips_second_and_restores();
    test_first_nonzero_runs_second_before_restoring();
    test_linked_nodes_then_priorities_run_in_order();
    test_cycle_is_terminal();
    return failures ? 1 : 0;
}
