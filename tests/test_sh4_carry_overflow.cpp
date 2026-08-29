#include "core/sh4_cfg.h"
#include "core/sh4_decoder.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static bool execute_words(const std::vector<std::uint16_t>& words,
                          jojo::Sh4ReferenceState& state,
                          std::uint32_t base = 0x8C010000u) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 2u);
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, base, base);
    CHECK(cfg);
    if (!cfg) return false;
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    if (!ir) return false;
    const auto run = jojo::execute_sh4_ir_reference(ir.value, state, {}, 32);
    CHECK(run);
    return static_cast<bool>(run);
}

static void test_decoder_patterns() {
    using jojo::Sh4Op;

    auto i = jojo::decode_sh4(0x312E, 0); // ADDC R2,R1
    CHECK(i.op == Sh4Op::addc_reg && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x312F, 0); // ADDV R2,R1
    CHECK(i.op == Sh4Op::addv_reg && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x312A, 0); // SUBC R2,R1
    CHECK(i.op == Sh4Op::subc_reg && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x312B, 0); // SUBV R2,R1
    CHECK(i.op == Sh4Op::subv_reg && i.rn == 1 && i.rm == 2);
    i = jojo::decode_sh4(0x612A, 0); // NEGC R2,R1
    CHECK(i.op == Sh4Op::negc_reg && i.rn == 1 && i.rm == 2);
}

static void test_addc_uses_input_t_and_reports_unsigned_carry() {
    jojo::Sh4ReferenceState carry_state{};
    carry_state.r[1] = 0xFFFFFFFFu;
    carry_state.r[2] = 0u;
    carry_state.t = true;
    carry_state.pr = 0xDEAD0000u;
    const bool carry_ok = execute_words({0x312E, 0x000B, 0x0009}, carry_state);
    if (!carry_ok) return;
    CHECK(carry_state.r[1] == 0u);
    CHECK(carry_state.r[2] == 0u);
    CHECK(carry_state.t);

    jojo::Sh4ReferenceState clear_state{};
    clear_state.r[1] = 1u;
    clear_state.r[2] = 2u;
    clear_state.t = true;
    clear_state.pr = 0xDEAD0010u;
    const bool clear_ok = execute_words({0x312E, 0x000B, 0x0009}, clear_state);
    if (!clear_ok) return;
    CHECK(clear_state.r[1] == 4u);
    CHECK(!clear_state.t);
}

static void test_subc_uses_input_t_and_reports_borrow() {
    jojo::Sh4ReferenceState borrow_state{};
    borrow_state.r[1] = 0u;
    borrow_state.r[2] = 0u;
    borrow_state.t = true;
    borrow_state.pr = 0xDEAD1000u;
    const bool borrow_ok = execute_words({0x312A, 0x000B, 0x0009}, borrow_state);
    if (!borrow_ok) return;
    CHECK(borrow_state.r[1] == 0xFFFFFFFFu);
    CHECK(borrow_state.t);

    jojo::Sh4ReferenceState clear_state{};
    clear_state.r[1] = 5u;
    clear_state.r[2] = 3u;
    clear_state.t = true;
    clear_state.pr = 0xDEAD1010u;
    const bool clear_ok = execute_words({0x312A, 0x000B, 0x0009}, clear_state);
    if (!clear_ok) return;
    CHECK(clear_state.r[1] == 1u);
    CHECK(!clear_state.t);
}

static void test_negc_reports_borrow() {
    jojo::Sh4ReferenceState borrow_state{};
    borrow_state.r[2] = 0u;
    borrow_state.t = true;
    borrow_state.pr = 0xDEAD2000u;
    const bool borrow_ok = execute_words({0x612A, 0x000B, 0x0009}, borrow_state);
    if (!borrow_ok) return;
    CHECK(borrow_state.r[1] == 0xFFFFFFFFu);
    CHECK(borrow_state.t);

    jojo::Sh4ReferenceState clear_state{};
    clear_state.r[2] = 0u;
    clear_state.t = false;
    clear_state.pr = 0xDEAD2010u;
    const bool clear_ok = execute_words({0x612A, 0x000B, 0x0009}, clear_state);
    if (!clear_ok) return;
    CHECK(clear_state.r[1] == 0u);
    CHECK(!clear_state.t);
}

static void test_addv_reports_signed_overflow_only() {
    jojo::Sh4ReferenceState overflow_state{};
    overflow_state.r[1] = 0x7FFFFFFFu;
    overflow_state.r[2] = 1u;
    overflow_state.t = false;
    overflow_state.pr = 0xDEAD3000u;
    const bool overflow_ok = execute_words({0x312F, 0x000B, 0x0009}, overflow_state);
    if (!overflow_ok) return;
    CHECK(overflow_state.r[1] == 0x80000000u);
    CHECK(overflow_state.t);

    jojo::Sh4ReferenceState clear_state{};
    clear_state.r[1] = 0xFFFFFFFFu;
    clear_state.r[2] = 1u;
    clear_state.t = true;
    clear_state.pr = 0xDEAD3010u;
    const bool clear_ok = execute_words({0x312F, 0x000B, 0x0009}, clear_state);
    if (!clear_ok) return;
    CHECK(clear_state.r[1] == 0u);
    CHECK(!clear_state.t);
}

static void test_subv_reports_signed_overflow_only() {
    jojo::Sh4ReferenceState overflow_state{};
    overflow_state.r[1] = 0x80000000u;
    overflow_state.r[2] = 1u;
    overflow_state.t = false;
    overflow_state.pr = 0xDEAD4000u;
    const bool overflow_ok = execute_words({0x312B, 0x000B, 0x0009}, overflow_state);
    if (!overflow_ok) return;
    CHECK(overflow_state.r[1] == 0x7FFFFFFFu);
    CHECK(overflow_state.t);

    jojo::Sh4ReferenceState clear_state{};
    clear_state.r[1] = 5u;
    clear_state.r[2] = 3u;
    clear_state.t = true;
    clear_state.pr = 0xDEAD4010u;
    const bool clear_ok = execute_words({0x312B, 0x000B, 0x0009}, clear_state);
    if (!clear_ok) return;
    CHECK(clear_state.r[1] == 2u);
    CHECK(!clear_state.t);
}

int main() {
    test_decoder_patterns();
    test_addc_uses_input_t_and_reports_unsigned_carry();
    test_subc_uses_input_t_and_reports_borrow();
    test_negc_reports_borrow();
    test_addv_reports_signed_overflow_only();
    test_subv_reports_signed_overflow_only();
    if (failures) {
        std::cerr << failures << " SH-4 carry/overflow arithmetic assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 carry/overflow arithmetic assertions passed\n";
    return 0;
}
