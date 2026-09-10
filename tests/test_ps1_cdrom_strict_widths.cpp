#include "core/ps1_memory_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1MemoryBus bus;
    bus.set_diagnostic_mmio_probe_enabled(true);

    const std::array<std::uint32_t, 4> cdrom_ports{
        0x1F801800u, 0x1F801801u, 0x1F801802u, 0x1F801803u,
    };

    for (const auto address : cdrom_ports) {
        CHECK(bus.read16(address).status == jojo::R3000aBusStatus::unsupported);
        CHECK(bus.read32(address).status == jojo::R3000aBusStatus::unsupported);
        CHECK(bus.write16(address, 0x1234u).status == jojo::R3000aBusStatus::unsupported);
        CHECK(bus.write32(address, 0x12345678u).status == jojo::R3000aBusStatus::unsupported);
    }

    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.read8(0x1F801802u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write8(0x1F801802u, 0x12u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    CHECK(bus.read32(0x1F801070u).status == jojo::R3000aBusStatus::unsupported);
    return failures ? 1 : 0;
}
