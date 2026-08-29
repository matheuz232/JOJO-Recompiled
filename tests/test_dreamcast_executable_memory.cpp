#include "core/dreamcast_memory.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::DreamcastBootProgram synthetic_program() {
    jojo::DreamcastBootProgram program{};
    program.bytes = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u};
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
    if (first) CHECK(first.value == 0x44332211u);
}

static void test_main_ram_aliases_share_the_same_storage() {
    auto loaded = jojo::load_dreamcast_boot_memory(synthetic_program());
    CHECK(loaded);
    if (!loaded) return;

    const auto physical = jojo::read_dreamcast_u32(loaded.value, 0x0C010000u);
    const auto cached = jojo::read_dreamcast_u32(loaded.value, 0x8C010000u);
    const auto uncached = jojo::read_dreamcast_u32(loaded.value, 0xAC010000u);
    CHECK(physical && cached && uncached);
    if (physical && cached && uncached) {
        CHECK(physical.value == cached.value);
        CHECK(cached.value == uncached.value);
    }

    const auto stored = jojo::write_dreamcast_u32(loaded.value, 0xAC010004u, 0xA1B2C3D4u);
    CHECK(stored);
    const auto mirrored = jojo::read_dreamcast_u32(loaded.value, 0x8C010004u);
    CHECK(mirrored);
    if (mirrored) CHECK(mirrored.value == 0xA1B2C3D4u);
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

static void test_loader_rejects_empty_or_crossing_programs() {
    jojo::DreamcastBootProgram empty{};
    const auto empty_load = jojo::load_dreamcast_boot_memory(empty);
    CHECK(!empty_load);
    if (!empty_load) CHECK(empty_load.error == jojo::ErrorCode::invalid_installation);

    auto program = synthetic_program();
    const auto crossing = jojo::load_dreamcast_boot_memory(program, 0x8CFFFFFCu);
    CHECK(!crossing);
    if (!crossing) CHECK(crossing.error == jojo::ErrorCode::invalid_argument);

    const auto non_ram = jojo::load_dreamcast_boot_memory(program, 0x8D000000u);
    CHECK(!non_ram);
    if (!non_ram) CHECK(non_ram.error == jojo::ErrorCode::invalid_argument);
}

int main() {
    test_loads_boot_program_into_main_ram();
    test_main_ram_aliases_share_the_same_storage();
    test_alignment_and_bounds_are_enforced();
    test_loader_rejects_empty_or_crossing_programs();
    if (failures) {
        std::cerr << failures << " Dreamcast executable-memory assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast executable-memory assertions passed\n";
    return 0;
}
