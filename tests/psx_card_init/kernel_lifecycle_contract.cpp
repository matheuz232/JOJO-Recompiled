#include "core/psx_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void lifecycle_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d BIOS lifecycle contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define LIFECYCLE_REQUIRE(expr) \
    do { if (!(expr)) lifecycle_fail(#expr, __LINE__); } while (0)

void call_b0(jojo::PsxRuntime& runtime, std::uint8_t function,
             std::uint32_t a0, std::uint32_t ra) {
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = a0;
    runtime.cpu.gpr[9] = function;
    runtime.cpu.gpr[31] = ra;
    runtime.cpu.gpr[2] = 0xdeadbeefu;
    const auto result = jojo::step_psx_runtime(runtime);
    LIFECYCLE_REQUIRE(result.reason == jojo::PsxR3000aStepReason::ok);
    LIFECYCLE_REQUIRE(runtime.cpu.gpr[2] == 1u);
    LIFECYCLE_REQUIRE(runtime.cpu.pc == ra);
    LIFECYCLE_REQUIRE(runtime.cpu.next_pc == ra + 4u);
}

void test_disable_and_close_event() {
    jojo::PsxRuntime runtime{};

    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 0xf0000009u;
    runtime.cpu.gpr[5] = 0x20u;
    runtime.cpu.gpr[6] = 0x2000u;
    runtime.cpu.gpr[7] = 0u;
    runtime.cpu.gpr[9] = 0x08u;
    runtime.cpu.gpr[31] = 0x80011000u;
    LIFECYCLE_REQUIRE(jojo::step_psx_runtime(runtime).reason ==
                      jojo::PsxR3000aStepReason::ok);
    const auto handle = runtime.cpu.gpr[2];
    LIFECYCLE_REQUIRE(handle == 0xf1000005u);

    call_b0(runtime, 0x0cu, handle, 0x80011010u); // EnableEvent
    LIFECYCLE_REQUIRE(runtime.bios.events[5].status == 0x2000u);

    call_b0(runtime, 0x0du, handle, 0x80011020u); // DisableEvent
    LIFECYCLE_REQUIRE(runtime.bios.events[5].allocated);
    LIFECYCLE_REQUIRE(runtime.bios.events[5].status == 0x1000u);

    call_b0(runtime, 0x09u, handle, 0x80011030u); // CloseEvent
    LIFECYCLE_REQUIRE(!runtime.bios.events[5].allocated);
    LIFECYCLE_REQUIRE(runtime.bios.events[5].status == 0u);

    // Retail BIOS returns 1 even for unused/invalid descriptors.
    call_b0(runtime, 0x0du, 0xf1ffffffu, 0x80011040u);
    call_b0(runtime, 0x09u, 0xf1ffffffu, 0x80011050u);
}

void test_close_thread_releases_tcb() {
    jojo::PsxRuntime runtime{};

    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 0x80020000u;
    runtime.cpu.gpr[5] = 0x801ff000u;
    runtime.cpu.gpr[6] = 0x80030000u;
    runtime.cpu.gpr[9] = 0x0eu;
    runtime.cpu.gpr[31] = 0x80011100u;
    LIFECYCLE_REQUIRE(jojo::step_psx_runtime(runtime).reason ==
                      jojo::PsxR3000aStepReason::ok);
    const auto handle = runtime.cpu.gpr[2];
    LIFECYCLE_REQUIRE(handle == 0xff000001u);
    LIFECYCLE_REQUIRE(runtime.bios.threads[1].allocated);

    call_b0(runtime, 0x0fu, handle, 0x80011110u); // CloseTh
    LIFECYCLE_REQUIRE(!runtime.bios.threads[1].allocated);

    // Closing an already-free or invalid thread still reports success.
    call_b0(runtime, 0x0fu, handle, 0x80011120u);
    call_b0(runtime, 0x0fu, 0xff00ffffu, 0x80011130u);
}

struct KernelLifecycleRunner {
    KernelLifecycleRunner() {
        test_disable_and_close_event();
        test_close_thread_releases_tcb();
    }
};

KernelLifecycleRunner kernel_lifecycle_runner{};

} // namespace
