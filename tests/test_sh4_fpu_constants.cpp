#include "core/sh4_cfg.h"
#include "core/sh4_decoder.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void test_fldi_constants_decode_and_execute_end_to_end() {
    constexpr std::uint32_t base = 0x8C010000u;
    const std::vector<std::uint8_t> bytes = {
        0x8Du, 0xF3u, // FLDI0 FR3
        0x9Du, 0xFAu, // FLDI1 FR10
    };

    const auto decoded = jojo::decode_sh4_stream(bytes, base);
    CHECK(decoded);
    if (!decoded) return;
    CHECK(decoded.value.size() == 2u);
    CHECK(decoded.value[0].op == jojo::Sh4Op::fldi0);
    CHECK(decoded.value[0].rn == 3u);
    CHECK(decoded.value[1].op == jojo::Sh4Op::fldi1);
    CHECK(decoded.value[1].rn == 10u);

    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    CHECK(cfg);
    if (!cfg) return;
    CHECK(cfg.value.unsupported_sites.empty());

    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return;

    jojo::Sh4ReferenceState state{};
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 4u);
    CHECK(run);
    if (!run) return;

    CHECK(state.fr[3] == 0x00000000u);
    CHECK(state.fr[10] == 0x3F800000u);
    CHECK(run.value.operations_executed == 2u);
    CHECK(run.value.stop_reason == jojo::Sh4ReferenceStopReason::end_of_stream);
}

int main() {
    test_fldi_constants_decode_and_execute_end_to_end();
    if (failures) {
        std::cerr << failures << " SH-4 FPU constant assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 FPU constant assertions passed\n";
    return 0;
}
