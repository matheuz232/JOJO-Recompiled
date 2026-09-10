#include "core/ps1_hle_bios.h"
#include "core/r3000a_state.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void test_default_hash_is_deterministic_and_state_changes_hash() {
    jojo::Ps1HleBios first;
    jojo::Ps1HleBios second;
    CHECK(first.diagnostic_state_hash() == second.diagnostic_state_hash());

    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010040u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::a0,
        0x39u,
        0x000000A0u,
        0x00004000u,
        0x00001000u,
        0u,
        0u,
        cpu.gpr[31],
    };
    CHECK(first.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(first.diagnostic_state_hash() != second.diagnostic_state_hash());
}

int main() {
    test_default_hash_is_deterministic_and_state_changes_hash();
    return failures ? 1 : 0;
}
