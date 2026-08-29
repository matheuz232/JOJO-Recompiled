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

static void test_decoder_recognizes_final_base_sh4_fpu_families() {
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
    state.fpscr = (state.fpscr & ~0x3u) | 0x00080000u; // PR=1, nearest/even.
    set_dr(state, 0, 1.5);
    set_dr(state, 2, 2.0);
    if (execute_words({0xF020u}, state, 0x8C071000u)) { // FADD DR2,DR0
        CHECK(get_dr(state, 0) == 3.5);
    }

    state.fpul = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(-7));
    if (execute_words({0xF42Du}, state, 0x8C071100u)) { // FLOAT FPUL,DR4
        CHECK(get_dr(state, 4) == -7.0);
    }
    set_dr(state, 6, 12.75);
    if (execute_words({0xF63Du}, state, 0x8C071200u)) { // FTRC DR6,FPUL
        CHECK(std::bit_cast<std::int32_t>(state.fpul) == 12);
    }
}

static void test_single_double_conversion_instructions() {
    jojo::Sh4ReferenceState state{};
    state.fpul = std::bit_cast<std::uint32_t>(1.5f);
    if (execute_words({0xF0ADu}, state, 0x8C072000u)) { // FCNVSD FPUL,DR0
        CHECK(get_dr(state, 0) == 1.5);
    }
    set_dr(state, 2, -2.25);
    if (execute_words({0xF2BDu}, state, 0x8C072100u)) { // FCNVDS DR2,FPUL
        CHECK(state.fpul == std::bit_cast<std::uint32_t>(-2.25f));
    }
}

static void test_vector_and_transcendental_families() {
    jojo::Sh4ReferenceState state{};
    state.fpscr &= ~0x00080000u; // PR=0.
    state.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    state.fr[1] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[2] = std::bit_cast<std::uint32_t>(3.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(4.0f);
    state.fr[4] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[5] = std::bit_cast<std::uint32_t>(3.0f);
    state.fr[6] = std::bit_cast<std::uint32_t>(4.0f);
    state.fr[7] = std::bit_cast<std::uint32_t>(5.0f);
    if (execute_words({0xF1EDu}, state, 0x8C073000u)) { // FIPR FV4,FV0
        CHECK(state.fr[3] == std::bit_cast<std::uint32_t>(40.0f));
    }

    for (int i = 0; i < 16; ++i) state.xf[static_cast<std::size_t>(i)] = 0u;
    for (int i = 0; i < 4; ++i) {
        state.xf[static_cast<std::size_t>(i * 4 + i)] = std::bit_cast<std::uint32_t>(1.0f);
    }
    state.fr[0] = std::bit_cast<std::uint32_t>(2.0f);
    state.fr[1] = std::bit_cast<std::uint32_t>(3.0f);
    state.fr[2] = std::bit_cast<std::uint32_t>(4.0f);
    state.fr[3] = std::bit_cast<std::uint32_t>(5.0f);
    if (execute_words({0xF1FDu}, state, 0x8C073100u)) { // FTRV XMTRX,FV0
        CHECK(state.fr[0] == std::bit_cast<std::uint32_t>(2.0f));
        CHECK(state.fr[1] == std::bit_cast<std::uint32_t>(3.0f));
        CHECK(state.fr[2] == std::bit_cast<std::uint32_t>(4.0f));
        CHECK(state.fr[3] == std::bit_cast<std::uint32_t>(5.0f));
    }

    state.fpul = 0u;
    if (execute_words({0xF0FDu}, state, 0x8C073200u)) { // FSCA FPUL,DR0
        CHECK(state.fr[0] == std::bit_cast<std::uint32_t>(0.0f));
        CHECK(state.fr[1] == std::bit_cast<std::uint32_t>(1.0f));
    }

    state.fr[0] = std::bit_cast<std::uint32_t>(4.0f);
    state.fpscr &= ~0x00080000u;
    state.fpscr &= ~(0x3Fu << 12u);
    state.fpscr &= ~(0x1Fu << 2u);
    if (execute_words({0xF07Du}, state, 0x8C073300u)) { // FSRRA FR0
        CHECK(state.fr[0] == std::bit_cast<std::uint32_t>(0.5f));
        CHECK((state.fpscr & (1u << 12u)) != 0u); // Cause.I is architecturally set for approximation.
        CHECK((state.fpscr & (1u << 2u)) != 0u);  // Flag.I accumulates.
    }
}

static void test_fpscr_exception_flags_and_enabled_exception_entry() {
    constexpr std::uint32_t kCauseZ = 1u << 15u;
    constexpr std::uint32_t kFlagZ = 1u << 5u;
    constexpr std::uint32_t kEnableZ = 1u << 10u;

    jojo::Sh4ReferenceState masked{};
    masked.fpscr &= ~0x00080000u;
    masked.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    masked.fr[1] = std::bit_cast<std::uint32_t>(0.0f);
    if (execute_words({0xF013u}, masked, 0x8C074000u)) { // FDIV FR1,FR0
        CHECK(std::isinf(std::bit_cast<float>(masked.fr[0])));
        CHECK((masked.fpscr & kCauseZ) != 0u);
        CHECK((masked.fpscr & kFlagZ) != 0u);
    }

    jojo::Sh4ReferenceState enabled{};
    enabled.fpscr &= ~0x00080000u;
    enabled.fpscr |= kEnableZ;
    enabled.fr[0] = std::bit_cast<std::uint32_t>(1.0f);
    enabled.fr[1] = std::bit_cast<std::uint32_t>(0.0f);
    enabled.vbr = 0x8C000000u;
    enabled.r[15] = 0x8CFF0000u;
    const auto before = enabled.fr[0];
    const auto run = run_words({0xF013u}, enabled, 0x8C074100u);
    CHECK(run);
    if (run) {
        CHECK(enabled.fr[0] == before); // trapping instruction must not commit destination.
        CHECK((enabled.fpscr & kCauseZ) != 0u);
        CHECK((enabled.fpscr & kFlagZ) != 0u);
        CHECK(enabled.expevt == 0x120u);
        CHECK(enabled.spc == 0x8C074100u);
        CHECK(enabled.sgr == 0x8CFF0000u);
        CHECK(enabled.pc == 0x8C000100u);
    }
}

int main() {
    test_decoder_recognizes_final_base_sh4_fpu_families();
    test_double_precision_arithmetic_and_conversion();
    test_single_double_conversion_instructions();
    test_vector_and_transcendental_families();
    test_fpscr_exception_flags_and_enabled_exception_entry();
    if (failures) {
        std::cerr << failures << " SH-4 final FPU completion assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 final FPU completion assertions passed\n";
    return 0;
}
