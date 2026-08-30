#include "core/psx_bus.h"
#include "core/psx_r3000a.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

int main() {
    jojo::PsxBus bus{};

    // Hardware raises pending interrupt bits; software acknowledges by writing
    // zero to bits it wants cleared while ones leave pending bits unchanged.
    bus.interrupt_status = 0x0555u;
    const auto seeded = jojo::psx_bus_read_u16(bus, 0x1f801070u);
    CHECK(seeded.reason == jojo::PsxBusAccessReason::ok);
    CHECK(seeded.value == 0x0555u);

    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801070u, 0x07f0u) == jojo::PsxBusAccessReason::ok);
    const auto acknowledged = jojo::psx_bus_read_u16(bus, 0x1f801070u);
    CHECK(acknowledged.reason == jojo::PsxBusAccessReason::ok);
    CHECK(acknowledged.value == static_cast<std::uint16_t>(0x0555u & 0x07f0u));

    bus.interrupt_status = 0xffffu;
    const auto masked = jojo::psx_bus_read_u16(bus, 0x1f801070u);
    CHECK(masked.reason == jojo::PsxBusAccessReason::ok);
    CHECK(masked.value == 0x07ffu);

    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c678u);
    cpu.gpr[3] = 0x1f801070u;
    cpu.gpr[2] = 0u;
    bus.interrupt_status = 0x07ffu;

    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x29, 3, 2, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003c67cu);
    CHECK(cpu.next_pc == 0x8003c680u);
    CHECK(bus.interrupt_status == 0u);

    if (failures) return 1;
    std::cout << "PSX I_STAT MMIO assertions passed\n";
    return 0;
}
