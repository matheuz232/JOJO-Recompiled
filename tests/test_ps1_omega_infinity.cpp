#include "core/ps1_omega_infinity.h"
#include "core/ps1_max3_explorer.h"

#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    const auto options = jojo::ps1_omega_infinity_options();
    CHECK(options.epoch_retired_limit == 3000000000ull);
    CHECK(options.chunk_target_bytes == 8ull * 1024ull * 1024ull);
    CHECK(options.max_session_disk_bytes == 16ull * 1024ull * 1024ull * 1024ull);
    CHECK(options.hot_trace_capacity == 262144u);
    CHECK(options.stop_on_strict_commercial_frame);

    jojo::Ps1OmegaInfinityControl control;
    CHECK(!control.stop_requested());
    control.request_stop();
    CHECK(control.stop_requested());

    const auto deep = jojo::ps1_max3_local_evidence_options();
    CHECK(deep.max_total_retired == 1000000000ull);

    const auto omega = jojo::ps1_max3_options(jojo::Ps1Max3Profile::omega);
    CHECK(omega.max_total_retired == 3000000000ull);

    return failures ? 1 : 0;
}
