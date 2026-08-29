#include "core/dreamcast_memory.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::DreamcastBootProgram synthetic_program() {
    jojo::DreamcastBootProgram program{};
    program.metadata.device_info = "GD-ROM1/1";
    program.bytes = {0x09u, 0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
    return program;
}

static void test_loads_boot_program_into_main_ram() {
    auto program = synthetic_program();
    const auto loaded = jojo::load_dreamcast_boot_memory(program);
    CHECK(loaded);
    if (!loaded) return;

    CHECK(loaded.value.main_ram.size() == jojo::kDreamcastMainRamSize);
    CHECK(loaded.value.load_address == jojo::kDreamcastBootLoadAddress);
    CHECK(loaded.value.entry_pc == jojo::kDreamcastBootLoadAddress);
    CHECK(loaded.value.program_size == program.bytes.size());

    const auto first = jojo::read_dreamcast_u32(loaded.value, 0x8C010000u);
    CHECK(first);
    if (first) CHECK(first.value == 0x22110009u);

    const auto zero_fill = jojo::read_dreamcast_u32(loaded.value, 0x8C020000u);
    CHECK(zero_fill);
    if (zero_fill) CHECK(zero_fill.value == 0u);
}

static void test_all_area3_main_ram_mirrors_share_backing_storage() {
    auto loaded = jojo::load_dreamcast_boot_memory(synthetic_program());
    CHECK(loaded);
    if (!loaded) return;

    const auto stored = jojo::write_dreamcast_u32(loaded.value, 0x0D000004u, 0xA1B2C3D4u);
    CHECK(stored);

    const auto physical_canonical = jojo::read_dreamcast_u32(loaded.value, 0x0C000004u);
    const auto physical_mirror2 = jojo::read_dreamcast_u32(loaded.value, 0x0E000004u);
    const auto physical_mirror3 = jojo::read_dreamcast_u32(loaded.value, 0x0F000004u);
    const auto cached_canonical = jojo::read_dreamcast_u32(loaded.value, 0x8C000004u);
    const auto cached_mirror = jojo::read_dreamcast_u32(loaded.value, 0x8F000004u);
    const auto uncached_canonical = jojo::read_dreamcast_u32(loaded.value, 0xAC000004u);
    const auto uncached_mirror = jojo::read_dreamcast_u32(loaded.value, 0xAE000004u);

    CHECK(physical_canonical && physical_mirror2 && physical_mirror3);
    CHECK(cached_canonical && cached_mirror && uncached_canonical && uncached_mirror);
    if (physical_canonical) CHECK(physical_canonical.value == 0xA1B2C3D4u);
    if (physical_mirror2) CHECK(physical_mirror2.value == 0xA1B2C3D4u);
    if (physical_mirror3) CHECK(physical_mirror3.value == 0xA1B2C3D4u);
    if (cached_canonical) CHECK(cached_canonical.value == 0xA1B2C3D4u);
    if (cached_mirror) CHECK(cached_mirror.value == 0xA1B2C3D4u);
    if (uncached_canonical) CHECK(uncached_canonical.value == 0xA1B2C3D4u);
    if (uncached_mirror) CHECK(uncached_mirror.value == 0xA1B2C3D4u);
}

static void test_alignment_and_bounds_are_enforced() {
    auto loaded = jojo::load_dreamcast_boot_memory(synthetic_program());
    CHECK(loaded);
    if (!loaded) return;

    const auto misaligned16 = jojo::read_dreamcast_u16(loaded.value, 0x8C010001u);
    CHECK(!misaligned16);
    if (!misaligned16) CHECK(misaligned16.error == jojo::ErrorCode::invalid_argument);

    const auto misaligned32 = jojo::write_dreamcast_u32(loaded.value, 0x8C010002u, 0u);
    CHECK(!misaligned32);
    if (!misaligned32) CHECK(misaligned32.error == jojo::ErrorCode::invalid_argument);

    const auto outside = jojo::read_dreamcast_u8(loaded.value, 0x8BFFFFFFu);
    CHECK(!outside);
    if (!outside) CHECK(outside.error == jojo::ErrorCode::invalid_argument);
}

static void test_loader_rejects_empty_crossing_or_non_area3_programs() {
    jojo::DreamcastBootProgram empty{};
    const auto empty_load = jojo::load_dreamcast_boot_memory(empty);
    CHECK(!empty_load);
    if (!empty_load) CHECK(empty_load.error == jojo::ErrorCode::invalid_installation);

    auto program = synthetic_program();
    const auto crossing = jojo::load_dreamcast_boot_memory(program, 0x8FFFFFFCu);
    CHECK(!crossing);
    if (!crossing) CHECK(crossing.error == jojo::ErrorCode::invalid_argument);

    const auto non_ram = jojo::load_dreamcast_boot_memory(program, 0x88000000u);
    CHECK(!non_ram);
    if (!non_ram) CHECK(non_ram.error == jojo::ErrorCode::invalid_argument);
}

static void test_prepare_executable_analyzes_then_loads_one_deterministic_image() {
    const auto prepared = jojo::prepare_dreamcast_executable(synthetic_program());
    CHECK(prepared);
    if (!prepared) return;

    CHECK(prepared.value.analysis.encoding == jojo::DreamcastBootEncoding::plain_gdrom);
    CHECK(prepared.value.analysis.load_address == jojo::kDreamcastBootLoadAddress);
    CHECK(prepared.value.analysis.unsupported_word_count == 0u);
    CHECK(prepared.value.memory.load_address == jojo::kDreamcastBootLoadAddress);
    CHECK(prepared.value.memory.entry_pc == jojo::kDreamcastBootLoadAddress);
    CHECK(prepared.value.memory.program_size == synthetic_program().bytes.size());

    const auto loaded_word = jojo::read_dreamcast_u16(prepared.value.memory, jojo::kDreamcastBootLoadAddress);
    CHECK(loaded_word);
    if (loaded_word) CHECK(loaded_word.value == 0x0009u);
}

static void test_prepare_executable_rejects_unanalyzable_media_before_loading() {
    auto milcd = synthetic_program();
    milcd.metadata.device_info = "CD-ROM1/1";
    const auto rejected = jojo::prepare_dreamcast_executable(milcd);
    CHECK(!rejected);
    if (!rejected) CHECK(rejected.error == jojo::ErrorCode::unsupported_format);
}

int main() {
    test_loads_boot_program_into_main_ram();
    test_all_area3_main_ram_mirrors_share_backing_storage();
    test_alignment_and_bounds_are_enforced();
    test_loader_rejects_empty_crossing_or_non_area3_programs();
    test_prepare_executable_analyzes_then_loads_one_deterministic_image();
    test_prepare_executable_rejects_unanalyzable_media_before_loading();
    if (failures) {
        std::cerr << failures << " Dreamcast executable-memory assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast executable-memory assertions passed\n";
    return 0;
}
