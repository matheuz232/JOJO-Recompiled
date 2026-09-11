#include "core/ps1_max3_candidate_engine.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

namespace {

std::uint32_t i_type(std::uint8_t opcode,
                     std::uint8_t rs,
                     std::uint8_t rt,
                     std::uint16_t immediate) {
    return (static_cast<std::uint32_t>(opcode) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           immediate;
}

std::vector<std::uint32_t> values(const std::vector<jojo::Ps1Max3ReadCandidate>& candidates) {
    std::vector<std::uint32_t> result;
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) result.push_back(candidate.value);
    return result;
}

bool equals(const std::vector<std::uint32_t>& actual,
            std::initializer_list<std::uint32_t> expected) {
    return actual == std::vector<std::uint32_t>(expected);
}

jojo::Ps1Max3CandidateContext context(jojo::Ps1Max3Profile profile,
                                      std::uint8_t width,
                                      std::size_t max_candidates = 16u) {
    jojo::Ps1Max3CandidateContext result{};
    result.profile = profile;
    result.width = width;
    result.pc = 0x80010000u;
    result.load_opcode = i_type(0x24u, 4u, 2u, 0u); // LBU v0, 0(a0)
    result.max_candidates = max_candidates;
    return result;
}

void test_strict_produces_no_speculative_candidates() {
    auto input = context(jojo::Ps1Max3Profile::strict, 1u);
    input.strict_observed_values = {0x12u};
    CHECK(jojo::generate_ps1_max3_read_candidates(input).empty());
}

void test_deep_baseline_is_width_masked_and_stable() {
    CHECK(equals(values(jojo::generate_ps1_max3_read_candidates(
                     context(jojo::Ps1Max3Profile::deep, 1u))),
                 {0u, 1u, 0xffu}));
    CHECK(equals(values(jojo::generate_ps1_max3_read_candidates(
                     context(jojo::Ps1Max3Profile::deep, 2u))),
                 {0u, 1u, 0xffffu}));
    CHECK(equals(values(jojo::generate_ps1_max3_read_candidates(
                     context(jojo::Ps1Max3Profile::deep, 4u))),
                 {0u, 1u, 0xffffffffu}));
}

void test_invalid_width_and_zero_cap_fail_closed() {
    CHECK(jojo::generate_ps1_max3_read_candidates(
              context(jojo::Ps1Max3Profile::deep, 3u)).empty());
    CHECK(jojo::generate_ps1_max3_read_candidates(
              context(jojo::Ps1Max3Profile::deep, 1u, 0u)).empty());
}

void test_strict_observations_are_masked_deduplicated_and_appended() {
    auto input = context(jojo::Ps1Max3Profile::deep, 1u);
    input.strict_observed_values = {0u, 0x42u, 0x142u, 1u, 0x7fu};
    const auto candidates = jojo::generate_ps1_max3_read_candidates(input);
    CHECK(equals(values(candidates), {0u, 1u, 0xffu, 0x42u, 0x7fu}));
    CHECK(candidates[3].source == jojo::Ps1Max3CandidateSource::strict_observed);
    CHECK(candidates[4].source == jojo::Ps1Max3CandidateSource::strict_observed);
}

void test_candidate_cap_preserves_deterministic_prefix() {
    auto input = context(jojo::Ps1Max3Profile::deep, 2u, 4u);
    input.strict_observed_values = {0x1234u, 0x5678u};
    CHECK(equals(values(jojo::generate_ps1_max3_read_candidates(input)),
                 {0u, 1u, 0xffffu, 0x1234u}));
}

void test_omega_adds_sign_bit_before_observed_and_constraints() {
    auto input = context(jojo::Ps1Max3Profile::omega, 1u);
    input.strict_observed_values = {0x42u};
    input.following_opcodes = {
        i_type(0x0cu, 2u, 2u, 0x0020u), // ANDI v0, v0, 0x20
    };
    const auto candidates = jojo::generate_ps1_max3_read_candidates(input);
    CHECK(equals(values(candidates), {0u, 1u, 0xffu, 0x80u, 0x42u, 0x20u}));
    CHECK(candidates[3].source == jojo::Ps1Max3CandidateSource::sign_bit);
    CHECK(candidates[4].source == jojo::Ps1Max3CandidateSource::strict_observed);
    CHECK(candidates[5].source == jojo::Ps1Max3CandidateSource::mask_class);
}

void test_omega_threshold_class_uses_loaded_register_and_bounded_window() {
    auto input = context(jojo::Ps1Max3Profile::omega, 2u);
    input.following_opcodes = {
        i_type(0x0bu, 2u, 3u, 10u), // SLTIU v1, v0, 10
    };
    const auto candidates = jojo::generate_ps1_max3_read_candidates(input);
    CHECK(equals(values(candidates),
                 {0u, 1u, 0xffffu, 0x8000u, 9u, 10u, 11u}));
    CHECK(candidates[4].source == jojo::Ps1Max3CandidateSource::threshold_class);
    CHECK(candidates[5].source == jojo::Ps1Max3CandidateSource::threshold_class);
    CHECK(candidates[6].source == jojo::Ps1Max3CandidateSource::threshold_class);

    auto unrelated = context(jojo::Ps1Max3Profile::omega, 2u);
    unrelated.following_opcodes = {
        i_type(0x0bu, 5u, 3u, 10u), // tests a1, not loaded v0
    };
    CHECK(equals(values(jojo::generate_ps1_max3_read_candidates(unrelated)),
                 {0u, 1u, 0xffffu, 0x8000u}));

    auto beyond_window = context(jojo::Ps1Max3Profile::omega, 2u);
    beyond_window.following_opcodes.assign(8u, 0u);
    beyond_window.following_opcodes.push_back(i_type(0x0bu, 2u, 3u, 10u));
    CHECK(equals(values(jojo::generate_ps1_max3_read_candidates(beyond_window)),
                 {0u, 1u, 0xffffu, 0x8000u}));
}

} // namespace

int main() {
    test_strict_produces_no_speculative_candidates();
    test_deep_baseline_is_width_masked_and_stable();
    test_invalid_width_and_zero_cap_fail_closed();
    test_strict_observations_are_masked_deduplicated_and_appended();
    test_candidate_cap_preserves_deterministic_prefix();
    test_omega_adds_sign_bit_before_observed_and_constraints();
    test_omega_threshold_class_uses_loaded_register_and_bounded_window();
    return failures ? 1 : 0;
}
