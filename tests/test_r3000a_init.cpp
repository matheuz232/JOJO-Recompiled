#include "core/ps1_exe.h"
#include "core/r3000a_reference_executor.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void check_zero_cop0(const jojo::R3000aState& s) {
    CHECK(s.cop0.target_address == 0u);
    CHECK(s.cop0.bad_vaddr == 0u);
    CHECK(s.cop0.status == 0u);
    CHECK(s.cop0.cause == 0u);
    CHECK(s.cop0.epc == 0u);
}

int main() {
    {
        jojo::Ps1ExeMetadata m{};
        m.entry_pc = 0x80010000u;
        m.initial_gp = 0x80018000u;
        m.stack_base = 0x801FFF00u;
        m.stack_size = 0x100u;

        const auto s = jojo::initialize_r3000a_for_psx_exe(m);
        CHECK(s.pc == 0x80010000u);
        CHECK(s.next_pc == 0x80010004u);
        CHECK(s.gpr[28] == 0x80018000u);
        CHECK(s.gpr[29] == 0x80200000u);
        CHECK(s.gpr[0] == 0u);
        for (std::size_t i = 1; i < s.gpr.size(); ++i) {
            if (i == 28u || i == 29u) continue;
            CHECK(s.gpr[i] == 0u);
        }
        CHECK(s.hi == 0u && s.lo == 0u);
        CHECK(!s.pending_load.valid);
        CHECK(!s.delay_slot.active);
        CHECK(s.external_interrupt_pending == 0u);
        check_zero_cop0(s);
    }

    // A PS-X EXE that does not request an initial stack leaves SP at zero.
    {
        jojo::Ps1ExeMetadata m{};
        m.entry_pc = 0x80010000u;
        m.initial_gp = 0x80018000u;
        m.stack_base = 0u;
        m.stack_size = 0u;

        const auto s = jojo::initialize_r3000a_for_psx_exe(m);
        CHECK(s.gpr[29] == 0u);
        CHECK(s.gpr[28] == 0x80018000u);
        CHECK(s.pc == m.entry_pc && s.next_pc == m.entry_pc + 4u);
        CHECK(!s.pending_load.valid && !s.delay_slot.active);
        CHECK(s.hi == 0u && s.lo == 0u);
        check_zero_cop0(s);
    }

    return failures ? 1 : 0;
}
