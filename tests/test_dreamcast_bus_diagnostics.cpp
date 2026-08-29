#include "core/dreamcast_boot_runner.h"
#include "core/dreamcast_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::DreamcastExecutableMemory blank_memory() {
    jojo::DreamcastBootProgram program{};
    program.bytes = {0x09u, 0x00u};
    const auto loaded = jojo::load_dreamcast_boot_memory(program);
    CHECK(loaded);
    return loaded ? loaded.value : jojo::DreamcastExecutableMemory{};
}

static void append_word(jojo::DreamcastBootProgram& program, std::uint16_t word) {
    program.bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    program.bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static void test_classifies_documented_dreamcast_regions() {
    using jojo::DreamcastBusRegion;
    CHECK(jojo::classify_dreamcast_bus_region(0x8C000100u) == DreamcastBusRegion::main_ram);
    CHECK(jojo::classify_dreamcast_bus_region(0xA05F6900u) == DreamcastBusRegion::system_asic);
    CHECK(jojo::classify_dreamcast_bus_region(0xA05F6C18u) == DreamcastBusRegion::maple);
    CHECK(jojo::classify_dreamcast_bus_region(0xA05F74E4u) == DreamcastBusRegion::gdrom_g1);
    CHECK(jojo::classify_dreamcast_bus_region(0xA05F8000u) == DreamcastBusRegion::pvr_registers);
    CHECK(jojo::classify_dreamcast_bus_region(0xA5000000u) == DreamcastBusRegion::pvr_vram);
    CHECK(jojo::classify_dreamcast_bus_region(0x10000000u) == DreamcastBusRegion::pvr_ta);
    CHECK(jojo::classify_dreamcast_bus_region(0xA0702C00u) == DreamcastBusRegion::aica_registers);
    CHECK(jojo::classify_dreamcast_bus_region(0xA0800000u) == DreamcastBusRegion::aica_wave_ram);
    CHECK(jojo::classify_dreamcast_bus_region(0xFF00001Cu) == DreamcastBusRegion::sh4_internal);
    CHECK(jojo::classify_dreamcast_bus_region(0x08000000u) == DreamcastBusRegion::unknown);
}

static void test_bus_records_first_unmapped_access() {
    auto memory = blank_memory();
    jojo::DreamcastReferenceBus bus(memory);

    const auto value = bus.read8(0xA05F8000u);
    CHECK(!value);
    const auto fault = bus.last_fault();
    CHECK(fault.has_value());
    if (!fault) return;
    CHECK(fault->address == 0xA05F8000u);
    CHECK(fault->region == jojo::DreamcastBusRegion::pvr_registers);
    CHECK(fault->access == jojo::DreamcastBusAccess::read);
    CHECK(fault->width_bytes == 1u);
}

static void test_boot_runner_surfaces_unmapped_hardware_stop() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u); // MOV.L R2,@R1

    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F8000u;
    initial.r[2] = 0x12345678u;

    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 8u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::unmapped_bus_access);
    CHECK(run.value.bus_fault.has_value());
    if (!run.value.bus_fault) return;
    CHECK(run.value.bus_fault->address == 0xA05F8000u);
    CHECK(run.value.bus_fault->region == jojo::DreamcastBusRegion::pvr_registers);
    CHECK(run.value.bus_fault->access == jojo::DreamcastBusAccess::write);
}

int main() {
    test_classifies_documented_dreamcast_regions();
    test_bus_records_first_unmapped_access();
    test_boot_runner_surfaces_unmapped_hardware_stop();
    if (failures) {
        std::cerr << failures << " Dreamcast bus-diagnostics assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast bus-diagnostics assertions passed\n";
    return 0;
}
