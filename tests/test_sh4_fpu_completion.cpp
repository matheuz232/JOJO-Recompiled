#include "core/sh4_cfg.h"
#include "core/sh4_decoder.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static jojo::Result<jojo::Sh4ReferenceRunResult> run_words(
    const std::vector<std::uint16_t>& words,
    jojo::Sh4ReferenceState& state,
    std::uint32_t base = 0x8C070000u) {
    std::vector<std::uint8_t> bytes;
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    if (!cfg) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(cfg.error, cfg.detail);
    if (!cfg.value.unsupported_sites.empty()) {
        return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(
            jojo::ErrorCode::unsupported_format, "test contains unsupported SH-4 opcode");
    }
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    if (!ir) return jojo::Result<jojo::Sh4ReferenceRunResult>::failure(ir.error, ir.detail);
    return jojo::execute_sh4_ir_reference(ir.value, state, {}, 32u);
}

static bool execute_words(const std::vector<std::uint16_t>& words,
                          jojo::Sh4ReferenceState& state,
                          std::uint32_t base = 0x8C070000u) {
    const auto result = run_words(words, state, base);
    CHECK(result);
    if (!result) std::cerr << "reference executor error: " << result.detail << '\n';
    return static_cast<bool>(result);
}

static void set_dr(jojo::Sh4ReferenceState& state, std::uint8_t even, double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    state.fr[even] = static_cast<std::uint32_t>(bits >> 32u);
    state.fr[even + 1u] = static_cast<std::uint32_t>(bits);
}

static double get_dr(const jojo::Sh4ReferenceState& state, std::uint8_t even) {
    const auto bits = (static_cast<std::uint64_t>(state.fr[even]) << 32u) |
                      static_cast<std::uint64_t>(state.fr[even + 1u]);
    return std::bit_cast<double>(bits);
}

static void test_decoder_recognizes_final_fpu_families() {
    using jojo::Sh4Op;
    CHECK(jojo::decode_sh4(0xF0ADu, 0).op == Sh4Op::fcnvsd);
    CHECK(jojo::decode_sh4(0xF0BDu, 0).op == Sh4Op::fcnvds);
    CHECK(jojo::decode_sh4(0xF1EDu, 0).op == Sh4Op::fipr);
    CHECK(jojo::decode_sh4(0xF0FDu, 0).op == Sh4Op::fsca);
    CHECK(jojo::decode_sh4(0xF07Du, 0).op == Sh4Op::fsrra);
    CHECK(jojo::decode_sh4(0xF1FDu, 0).op == Sh4Op::ftrv);
}

static void test_double_precision_arithmetic_and_conversion() {
    jojo::Sh4ReferenceState state{};
    state.fpscr = (state.fpscr & ~0x00300003u) | 0x00080000u; // PR=1,SZ=0,nearest.
    set_dr(state, 0, 1.5);
    set_dr(state, 2, 2.0);
    if (execute_words({0xF020u}, state, 0x8C071000u)) CHECK(get_dr(state, 0) == 3.5);

    state.fpul = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(-7));
    if (execute_words({0xF42Du}, state, 0x8C071100u)) CHECK(get_dr(state, 4) == -7.0);

    set_dr(state, 6, 12.75);
    if (execute_words({0xF63Du}, state, 0x8C071200u)) {
        CHECK(std::bit_cast<std::int32_t>(state.fpul) == 12);
    }
}

static void test_single_double_conversion_requires_double_precision_mode() {
    jojo::Sh4ReferenceState state{};
    state.fpscr = (state.fpscr & ~0x00300000u) | 0x00080000u; // PR=1,SZ=0.
    state.fpul = std::bit_cast<std::uint32_t>(1.5f);
    if (execute_words({0xF0ADu}, state, 0x8C072000u)) CHECK(get_dr(state, 0) == 1.5);

    set_dr(state, 2, -2.25);
    if (execute_words({0xF2BDu}, state, 0x8C072100u)) {
        CHECK(state.fpul == std::bit_cast<std::uint32_t>(-2.25f));
    }

    jojo::Sh4ReferenceState wrong_mode{}; // PR=0.
    wrong_mode.vbr = 0x8C000000u;
    wrong_mode.fpul = std::bit_cast<std::uint32_t>(3.0f);
    const auto before0 = wrong_mode.fr[0];
    const auto run = run_words({0xF0ADu}, wrong_mode, 0x8C072200u);
    CHECK(run);
    if (run) {
        CHECK(wrong_mode.fr[0] == before0);
        CHECK(wrong_mode.expevt == 0x180u);
        CHECK(wrong_mode.spc == 0x8C072200u);
    }
}

static void test_vector_families_signal_architectural_inexact() {
    constexpr std::uint32_t kCauseI = 1u << 12u;
    constexpr std::uint32_t kFlagI = 1u << 2u;

    jojo::Sh4ReferenceState fipr{};
    fipr.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    fipr.fr[1] = std::bit_cast<std::uint32_t>(2.0f);
    fipr.fr[2] = std::bit_cast<std::uint32_t>(3.0f);
    fipr.fr[3] = std::bit_cast<std::uint32_t>(4.0f);
    fipr.fr[4] = std::bit_cast<std::uint32_t>(2.0f);
    fipr.fr[5] = std::bit_cast<std::uint32_t>(3.0f);
    fipr.fr[6] = std::bit_cast<std::uint32_t>(4.0f);
    fipr.fr[7] = std::bit_cast<std::uint32_t>(5.0f);
    if (execute_words({0xF1EDu}, fipr, 0x8C073000u)) {
        CHECK(fipr.fr[3] == std::bit_cast<std::uint32_t>(40.0f));
        CHECK((fipr.fpscr & kCauseI) != 0u);
        CHECK((fipr.fpscr & kFlagI) != 0u);
    }

    jojo::Sh4ReferenceState ftrv{};
    for (int i = 0; i < 16; ++i) ftrv.xf[static_cast<std::size_t>(i)] = 0u;
    for (int i = 0; i < 4; ++i) {
        ftrv.xf[static_cast<std::size_t>(i * 4 + i)] = std::bit_cast<std::uint32_t>(1.0f);
    }
    ftrv.fr[0] = std::bit_cast<std::uint32_t>(2.0f);
    ftrv.fr[1] = std::bit_cast<std::uint32_t>(3.0f);
    ftrv.fr[2] = std::bit_cast<std::uint32_t>(4.0f);
    ftrv.fr[3] = std::bit_cast<std::uint32_t>(5.0f);
    if (execute_words({0xF1FDu}, ftrv, 0x8C073100u)) {
        CHECK(ftrv.fr[0] == std::bit_cast<std::uint32_t>(2.0f));
        CHECK(ftrv.fr[1] == std::bit_cast<std::uint32_t>(3.0f));
        CHECK(ftrv.fr[2] == std::bit_cast<std::uint32_t>(4.0f));
        CHECK(ftrv.fr[3] == std::bit_cast<std::uint32_t>(5.0f));
        CHECK((ftrv.fpscr & kCauseI) != 0u);
        CHECK((ftrv.fpscr & kFlagI) != 0u);
    }
}

static void test_enabled_inexact_on_fipr_traps_before_writeback() {
    constexpr std::uint32_t kEnableI = 1u << 7u;
    constexpr std::uint32_t kCauseI = 1u << 12u;
    jojo::Sh4ReferenceState state{};
    state.fpscr |= kEnableI;
    state.vbr = 0x8C000000u;
    for (int i = 0; i < 8; ++i) state.fr[static_cast<std::size_t>(i)] = std::bit_cast<std::uint32_t>(1.0f);
    const auto before = state.fr[3];
    const auto run = run_words({0xF1EDu}, state, 0x8C073180u);
    CHECK(run);
    if (!run) return;
    CHECK(state.fr[3] == before);
    CHECK((state.fpscr & kCauseI) != 0u);
    CHECK(state.expevt == 0x120u);
    CHECK(state.spc == 0x8C073180u);
    CHECK(state.pc == 0x8C000100u);
}

static void test_fsca_and_fsrra() {
    constexpr std::uint32_t kCauseI = 1u << 12u;
    constexpr std::uint32_t kFlagI = 1u << 2u;
    constexpr std::uint32_t kEnableI = 1u << 7u;
    constexpr std::uint32_t kCauseE = 1u << 17u;

    jojo::Sh4ReferenceState state{};
    state.fpul = 0u;
    if (execute_words({0xF0FDu}, state, 0x8C073200u)) {
        CHECK(state.fr[0] == std::bit_cast<std::uint32_t>(0.0f));
        CHECK(state.fr[1] == std::bit_cast<std::uint32_t>(1.0f));
        CHECK((state.fpscr & kCauseI) != 0u);
        CHECK((state.fpscr & kFlagI) != 0u);
    }

    jojo::Sh4ReferenceState fsca_trap{};
    fsca_trap.fpscr |= kEnableI;
    fsca_trap.vbr = 0x8C000000u;
    fsca_trap.fpul = 0u;
    fsca_trap.fr[0] = 0x11111111u;
    fsca_trap.fr[1] = 0x22222222u;
    const auto fsca_run = run_words({0xF0FDu}, fsca_trap, 0x8C073280u);
    CHECK(fsca_run);
    if (fsca_run) {
        CHECK(fsca_trap.fr[0] == 0x11111111u);
        CHECK(fsca_trap.fr[1] == 0x22222222u);
        CHECK((fsca_trap.fpscr & kCauseI) != 0u);
        CHECK(fsca_trap.expevt == 0x120u);
        CHECK(fsca_trap.spc == 0x8C073280u);
    }

    jojo::Sh4ReferenceState fsrra{};
    fsrra.fr[0] = std::bit_cast<std::uint32_t>(4.0f);
    if (execute_words({0xF07Du}, fsrra, 0x8C073300u)) {
        CHECK(fsrra.fr[0] == std::bit_cast<std::uint32_t>(0.5f));
        CHECK((fsrra.fpscr & kCauseI) != 0u);
        CHECK((fsrra.fpscr & kFlagI) != 0u);
    }

    jojo::Sh4ReferenceState fsrra_denormal{};
    fsrra_denormal.fpscr &= ~(1u << 18u); // DN=0.
    fsrra_denormal.vbr = 0x8C000000u;
    fsrra_denormal.fr[0] = 0x00000001u;
    const auto before_denormal = fsrra_denormal.fr[0];
    const auto fsrra_run = run_words({0xF07Du}, fsrra_denormal, 0x8C073380u);
    CHECK(fsrra_run);
    if (fsrra_run) {
        CHECK(fsrra_denormal.fr[0] == before_denormal);
        CHECK((fsrra_denormal.fpscr & kCauseE) != 0u);
        CHECK(fsrra_denormal.expevt == 0x120u);
        CHECK(fsrra_denormal.spc == 0x8C073380u);
    }
}

static void test_fpscr_divide_exception_and_unmaskable_denormal_error() {
    constexpr std::uint32_t kCauseZ = 1u << 15u;
    constexpr std::uint32_t kFlagZ = 1u << 5u;
    constexpr std::uint32_t kEnableZ = 1u << 10u;
    constexpr std::uint32_t kCauseE = 1u << 17u;

    jojo::Sh4ReferenceState enabled{};
    enabled.fpscr |= kEnableZ;
    enabled.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    enabled.fr[1] = std::bit_cast<std::uint32_t>(0.0f);
    enabled.vbr = 0x8C000000u;
    const auto before = enabled.fr[0];
    auto run = run_words({0xF013u}, enabled, 0x8C074100u);
    CHECK(run);
    if (run) {
        CHECK(enabled.fr[0] == before);
        CHECK((enabled.fpscr & kCauseZ) != 0u);
        CHECK((enabled.fpscr & kFlagZ) != 0u);
        CHECK(enabled.expevt == 0x120u);
    }

    jojo::Sh4ReferenceState denormal{};
    denormal.fpscr &= ~(1u << 18u); // DN=0: denormal input must raise unmaskable FPU error E.
    denormal.vbr = 0x8C000000u;
    denormal.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    denormal.fr[1] = 0x00000001u;
    const auto denormal_before = denormal.fr[0];
    run = run_words({0xF010u}, denormal, 0x8C074200u); // FADD FR1,FR0
    CHECK(run);
    if (run) {
        CHECK(denormal.fr[0] == denormal_before);
        CHECK((denormal.fpscr & kCauseE) != 0u);
        CHECK(denormal.expevt == 0x120u);
        CHECK(denormal.spc == 0x8C074200u);
        CHECK(denormal.pc == 0x8C000100u);
    }
}

int main() {
    test_decoder_recognizes_final_fpu_families();
    test_double_precision_arithmetic_and_conversion();
    test_single_double_conversion_requires_double_precision_mode();
    test_vector_families_signal_architectural_inexact();
    test_enabled_inexact_on_fipr_traps_before_writeback();
    test_fsca_and_fsrra();
    test_fpscr_divide_exception_and_unmaskable_denormal_error();
    if (failures) {
        std::cerr << failures << " SH-4 final FPU completion assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 final FPU completion assertions passed\n";
    return 0;
}
