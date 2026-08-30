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

    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801074u, 0xffffu) == jojo::PsxBusAccessReason::ok);
    const auto seeded = jojo::psx_bus_read_u16(bus, 0x1f801074u);
    CHECK(seeded.reason == jojo::PsxBusAccessReason::ok);
    CHECK(seeded.value == 0x07ffu);

    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c66cu);
    cpu.gpr[2] = 0x1f801074u;
    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x29, 2, 0, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003c670u);

    const auto masked = jojo::psx_bus_read_u16(bus, 0x1f801074u);
    CHECK(masked.reason == jojo::PsxBusAccessReason::ok);
    CHECK(masked.value == 0u);

    if (failures) return 1;
    std::cout << "PSX I_MASK MMIO assertions passed\n";
    return 0;
}
