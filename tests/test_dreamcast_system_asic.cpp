#include "core/dreamcast_bus.h"
#include "core/dreamcast_system_asic.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t read32(jojo::DreamcastReferenceBus& bus, std::uint32_t address) {
    const auto b0 = bus.read8(address + 0u);
    const auto b1 = bus.read8(address + 1u);
    const auto b2 = bus.read8(address + 2u);
    const auto b3 = bus.read8(address + 3u);
    CHECK(b0 && b1 && b2 && b3);
    if (!(b0 && b1 && b2 && b3)) return 0u;
    return static_cast<std::uint32_t>(b0.value) |
           (static_cast<std::uint32_t>(b1.value) << 8u) |
           (static_cast<std::uint32_t>(b2.value) << 16u) |
           (static_cast<std::uint32_t>(b3.value) << 24u);
}

static void write32(jojo::DreamcastReferenceBus& bus, std::uint32_t address, std::uint32_t value) {
    CHECK(bus.write8(address + 0u, static_cast<std::uint8_t>(value)));
    CHECK(bus.write8(address + 1u, static_cast<std::uint8_t>(value >> 8u)));
    CHECK(bus.write8(address + 2u, static_cast<std::uint8_t>(value >> 16u)));
    CHECK(bus.write8(address + 3u, static_cast<std::uint8_t>(value >> 24u)));
}

static jojo::DreamcastExecutableMemory blank_memory() {
    jojo::DreamcastBootProgram program{};
    program.bytes = {0x09u, 0x00u};
    const auto loaded = jojo::load_dreamcast_boot_memory(program);
    CHECK(loaded);
    return loaded ? loaded.value : jojo::DreamcastExecutableMemory{};
}

static void test_interrupt_masks_are_byte_addressable_through_bus() {
    auto memory = blank_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastReferenceBus bus(memory);
    bus.attach_device(jojo::DreamcastBusRegion::system_asic, asic);

    write32(bus, 0x005F6910u, 0x12345678u);
    write32(bus, 0x005F6924u, 0xA5A55A5Au);
    write32(bus, 0x005F6938u, 0x80000001u);

    CHECK(read32(bus, 0x005F6910u) == 0x12345678u);
    CHECK(read32(bus, 0x005F6924u) == 0xA5A55A5Au);
    CHECK(read32(bus, 0x005F6938u) == 0x80000001u);
}

static void test_status_bits_are_raised_and_acknowledged_write_one_to_clear() {
    auto memory = blank_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastReferenceBus bus(memory);
    bus.attach_device(jojo::DreamcastBusRegion::system_asic, asic);

    asic.raise_normal(0x00001004u);
    asic.raise_external(0x00000002u);
    asic.raise_error(0x00002000u);

    CHECK(read32(bus, 0x005F6900u) == 0x00001004u);
    CHECK(read32(bus, 0x005F6904u) == 0x00000002u);
    CHECK(read32(bus, 0x005F6908u) == 0x00002000u);

    write32(bus, 0x005F6900u, 0x00000004u);
    write32(bus, 0x005F6904u, 0x00000002u);
    CHECK(read32(bus, 0x005F6900u) == 0x00001000u);
    CHECK(read32(bus, 0x005F6904u) == 0x00000000u);
    CHECK(read32(bus, 0x005F6908u) == 0x00002000u);
}

static void test_unimplemented_system_asic_offsets_fail_instead_of_faking_values() {
    auto memory = blank_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastReferenceBus bus(memory);
    bus.attach_device(jojo::DreamcastBusRegion::system_asic, asic);

    const auto read = bus.read8(0x005F6800u);
    const auto write = bus.write8(0x005F6800u, 0x55u);
    CHECK(!read);
    CHECK(!write);
}

int main() {
    test_interrupt_masks_are_byte_addressable_through_bus();
    test_status_bits_are_raised_and_acknowledged_write_one_to_clear();
    test_unimplemented_system_asic_offsets_fail_instead_of_faking_values();
    if (failures) {
        std::cerr << failures << " Dreamcast System ASIC assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast System ASIC assertions passed\n";
    return 0;
}
