#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

int main() {
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 0u;              // Real SLUS_010.60 InitCARD2(pad_enable=0).
    runtime.cpu.gpr[9] = 0x4au;
    runtime.cpu.gpr[31] = 0x80045068u;    // Real return address.
    runtime.cpu.gpr[2] = 0x2468ace0u;     // Function has no documented return value.

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.bios.card_initialized);
    CHECK(!runtime.bios.card_pad_enabled);
    CHECK(runtime.bios.card_early_irq_handler_installed);
    CHECK(runtime.cpu.gpr[2] == 0x2468ace0u);
    CHECK(runtime.cpu.pc == 0x80045068u);
    CHECK(runtime.cpu.next_pc == 0x8004506cu);

    if (failures) return 1;
    std::cout << "PSX InitCARD2 frontier assertions passed\n";
    return 0;
}
