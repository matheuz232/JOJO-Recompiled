#include "core/ps1_memory_bus.h"
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1MemoryBus bus;

    CHECK(bus.write32(0x00000100u, 0x44332211u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x80000100u).value == 0x11u);
    CHECK(bus.read8(0xA0000101u).value == 0x22u);
    CHECK(bus.read16(0x00000102u).value == 0x4433u);
    CHECK(bus.read32(0x80000100u).value == 0x44332211u);

    CHECK(bus.write16(0x1F800010u, 0xBBAAu).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read16(0x9F800010u).value == 0xBBAAu);
    CHECK(bus.read16(0xBF800010u).value == 0xBBAAu);
    CHECK(bus.read16(0x00000010u).value != 0xBBAAu);

    CHECK(bus.write16(0x1F801074u, 0xFFFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_mask() == 0x07FFu);
    const auto imask_enabled = bus.read16(0x1F801074u);
    CHECK(imask_enabled.status == jojo::R3000aBusStatus::ok);
    CHECK(imask_enabled.value == 0x07FFu);

    CHECK(bus.write16(0x1F801074u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_mask() == 0x0000u);
    const auto imask_disabled = bus.read16(0x1F801074u);
    CHECK(imask_disabled.status == jojo::R3000aBusStatus::ok);
    CHECK(imask_disabled.value == 0x0000u);

    CHECK(bus.read32(0x1F801074u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read16(0x1F801070u).status == jojo::R3000aBusStatus::unsupported);

    const auto unsupported = bus.read32(0x1F801070u);
    CHECK(unsupported.status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.last_unsupported_access().has_value());
    if (bus.last_unsupported_access()) {
        CHECK(bus.last_unsupported_access()->guest_address == 0x1F801070u);
        CHECK(bus.last_unsupported_access()->physical_address == 0x1F801070u);
        CHECK(bus.last_unsupported_access()->width == 4u);
        CHECK(!bus.last_unsupported_access()->write);
    }

    CHECK(bus.read8(0x00200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read8(0x80200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read8(0xA0200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read32(0xC0000000u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
