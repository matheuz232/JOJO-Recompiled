#include "core/sh4_opcode_census.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static void test_census_counts_and_groups_unknown_words() {
    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0x0009u);
    append_word(bytes, 0xFFFFu);
    append_word(bytes, 0xE001u);
    append_word(bytes, 0xFFFFu);
    append_word(bytes, 0xF123u);

    const auto census = jojo::analyze_sh4_opcode_census(bytes, 0x8C010000u, 4u);
    CHECK(census);
    if (!census) return;

    CHECK(census.value.total_words == 5u);
    CHECK(census.value.supported_words == 2u);
    CHECK(census.value.unsupported_words == 3u);
    CHECK(census.value.unsupported.size() == 2u);
    if (census.value.unsupported.size() != 2u) return;

    CHECK(census.value.unsupported[0].raw == 0xFFFFu);
    CHECK(census.value.unsupported[0].count == 2u);
    CHECK(census.value.unsupported[0].sample_addresses.size() == 2u);
    CHECK(census.value.unsupported[0].sample_addresses[0] == 0x8C010002u);
    CHECK(census.value.unsupported[0].sample_addresses[1] == 0x8C010006u);

    CHECK(census.value.unsupported[1].raw == 0xF123u);
    CHECK(census.value.unsupported[1].count == 1u);
    CHECK(census.value.unsupported[1].sample_addresses.size() == 1u);
    CHECK(census.value.unsupported[1].sample_addresses[0] == 0x8C010008u);
}

static void test_census_limits_samples_without_losing_frequency() {
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 6; ++i) append_word(bytes, 0xFFFFu);

    const auto census = jojo::analyze_sh4_opcode_census(bytes, 0x1000u, 2u);
    CHECK(census);
    if (!census || census.value.unsupported.empty()) return;
    CHECK(census.value.unsupported[0].count == 6u);
    CHECK(census.value.unsupported[0].sample_addresses.size() == 2u);
    CHECK(census.value.unsupported[0].sample_addresses[0] == 0x1000u);
    CHECK(census.value.unsupported[0].sample_addresses[1] == 0x1002u);
}

static void test_census_rejects_invalid_streams() {
    const auto odd = jojo::analyze_sh4_opcode_census({0x09u}, 0x1000u, 4u);
    CHECK(!odd);
    CHECK(odd.error == jojo::ErrorCode::invalid_argument);

    std::vector<std::uint8_t> bytes;
    append_word(bytes, 0x0009u);
    append_word(bytes, 0x0009u);
    const auto overflow = jojo::analyze_sh4_opcode_census(bytes, 0xFFFFFFFEu, 4u);
    CHECK(!overflow);
    CHECK(overflow.error == jojo::ErrorCode::invalid_argument);
}

int main() {
    test_census_counts_and_groups_unknown_words();
    test_census_limits_samples_without_losing_frequency();
    test_census_rejects_invalid_streams();
    if (failures) {
        std::cerr << failures << " SH-4 opcode census assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 opcode census assertions passed\n";
    return 0;
}
