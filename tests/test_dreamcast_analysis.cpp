#include "core/dreamcast_analysis.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static jojo::DreamcastBootProgram make_program(std::string device_info) {
    jojo::DreamcastBootProgram program{};
    program.metadata.hardware_id = "SEGA SEGAKATANA";
    program.metadata.device_info = std::move(device_info);
    program.metadata.boot_filename = "BOOT.BIN";
    return program;
}

static void test_classifies_media_conservatively() {
    auto program = make_program("GD-ROM1/1");
    CHECK(jojo::classify_dreamcast_boot_encoding(program.metadata) ==
          jojo::DreamcastBootEncoding::plain_gdrom);

    program.metadata.device_info = "CD-ROM1/1";
    CHECK(jojo::classify_dreamcast_boot_encoding(program.metadata) ==
          jojo::DreamcastBootEncoding::milcd_requires_normalization);

    program.metadata.device_info = "MYSTERY";
    CHECK(jojo::classify_dreamcast_boot_encoding(program.metadata) ==
          jojo::DreamcastBootEncoding::unknown);
}

static void test_reports_decoder_coverage_and_cfg() {
    auto program = make_program("GD-ROM1/1");
    append_word(program.bytes, 0xE001); // MOV #1,R0
    append_word(program.bytes, 0x0009); // NOP
    append_word(program.bytes, 0xFFFF); // unsupported
    append_word(program.bytes, 0xFFFF); // unsupported (linear diagnostic)

    const auto result = jojo::analyze_dreamcast_boot_program(program);
    CHECK(result);
    if (!result) return;
    CHECK(result.value.encoding == jojo::DreamcastBootEncoding::plain_gdrom);
    CHECK(result.value.load_address == 0x8C010000u);
    CHECK(result.value.word_count == 4);
    CHECK(result.value.supported_word_count == 2);
    CHECK(result.value.unsupported_word_count == 2);
    CHECK(result.value.unsupported_histogram.size() == 1);
    if (!result.value.unsupported_histogram.empty()) {
        CHECK(result.value.unsupported_histogram[0].raw_opcode == 0xFFFFu);
        CHECK(result.value.unsupported_histogram[0].count == 2);
    }
    CHECK(result.value.entry_cfg.entry_address == 0x8C010000u);
    CHECK(result.value.entry_cfg.blocks.size() == 1);
    CHECK(result.value.entry_cfg.unsupported_sites.size() == 1);
    CHECK(result.value.entry_cfg.unsupported_sites[0] == 0x8C010004u);
}

static void test_histogram_is_count_then_opcode_sorted() {
    auto program = make_program("GD-ROM1/1");
    append_word(program.bytes, 0xFFFE);
    append_word(program.bytes, 0xFFFF);
    append_word(program.bytes, 0xFFFE);
    append_word(program.bytes, 0xFFFD);
    append_word(program.bytes, 0xFFFF);
    append_word(program.bytes, 0xFFFE);

    const auto result = jojo::analyze_dreamcast_boot_program(program);
    CHECK(result);
    if (!result) return;
    CHECK(result.value.unsupported_histogram.size() == 3);
    if (result.value.unsupported_histogram.size() == 3) {
        CHECK(result.value.unsupported_histogram[0].raw_opcode == 0xFFFEu);
        CHECK(result.value.unsupported_histogram[0].count == 3);
        CHECK(result.value.unsupported_histogram[1].raw_opcode == 0xFFFFu);
        CHECK(result.value.unsupported_histogram[1].count == 2);
        CHECK(result.value.unsupported_histogram[2].raw_opcode == 0xFFFDu);
        CHECK(result.value.unsupported_histogram[2].count == 1);
    }
}

static void test_refuses_unnormalized_milcd_and_unknown_media() {
    auto milcd = make_program("CD-ROM1/1");
    append_word(milcd.bytes, 0x0009);
    const auto milcd_result = jojo::analyze_dreamcast_boot_program(milcd);
    CHECK(!milcd_result);
    CHECK(milcd_result.error == jojo::ErrorCode::unsupported_format);

    auto unknown = make_program("???");
    append_word(unknown.bytes, 0x0009);
    const auto unknown_result = jojo::analyze_dreamcast_boot_program(unknown);
    CHECK(!unknown_result);
    CHECK(unknown_result.error == jojo::ErrorCode::unsupported_format);
}

static void test_supported_return_cfg() {
    auto program = make_program("GD-ROM1/1");
    append_word(program.bytes, 0x000B); // RTS
    append_word(program.bytes, 0x0009); // delay NOP
    const auto result = jojo::analyze_dreamcast_boot_program(program);
    CHECK(result);
    if (result) {
        CHECK(result.value.supported_word_count == 2);
        CHECK(result.value.unsupported_word_count == 0);
        CHECK(result.value.entry_cfg.blocks.size() == 1);
        CHECK(result.value.entry_cfg.blocks[0].exit == jojo::Sh4BlockExit::return_subroutine);
    }
}

int main() {
    test_classifies_media_conservatively();
    test_reports_decoder_coverage_and_cfg();
    test_histogram_is_count_then_opcode_sorted();
    test_refuses_unnormalized_milcd_and_unknown_media();
    test_supported_return_cfg();
    if (failures) {
        std::cerr << failures << " Dreamcast analysis assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast analysis assertions passed\n";
    return 0;
}
