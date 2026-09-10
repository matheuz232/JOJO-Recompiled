#include "core/ps1_interrupt_continuation.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
    << " CHECK failed: " #x "\n"; ++failures; } } while (0)

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

int main() {
    test_begin_and_restore_exact_v0_context();
    test_equal_continuations_hash_equal();
    return failures ? 1 : 0;
}
