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

    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F801074u, 0x00000001u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_mask() == 0x0001u);
    const auto imask32 = bus.read32(0x1F801074u);
    CHECK(imask32.status == jojo::R3000aBusStatus::ok);
    CHECK(imask32.value == 0x00000001u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F801070u, 0xFFFFFFFEu).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());

    bus.clear_last_diagnostic_mmio_probe();
    const auto before_common_delay = bus.diagnostic_state_hash();
    CHECK(bus.write32(0x1F801020u, 0x00001325u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.diagnostic_state_hash() != before_common_delay);
    bus.set_diagnostic_mmio_probe_enabled(false);

    CHECK(bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    const auto istat_cleared = bus.read16(0x1F801070u);
    CHECK(istat_cleared.status == jojo::R3000aBusStatus::ok);
    CHECK(istat_cleared.value == 0x0000u);
    const auto istat32_cleared = bus.read32(0x1F801070u);
    CHECK(istat32_cleared.status == jojo::R3000aBusStatus::ok);
    CHECK(istat32_cleared.value == 0x00000000u);
    CHECK(bus.read32(0x1F801020u).status == jojo::R3000aBusStatus::unsupported);

    const auto dpcr_reset = bus.read32(0x1F8010F0u);
    CHECK(dpcr_reset.status == jojo::R3000aBusStatus::ok);
    CHECK(dpcr_reset.value == 0x07654321u);
    CHECK(bus.write32(0x1F8010F0u, 0x33333333u).status == jojo::R3000aBusStatus::ok);
    const auto dpcr_written = bus.read32(0x1F8010F0u);
    CHECK(dpcr_written.status == jojo::R3000aBusStatus::ok);
    CHECK(dpcr_written.value == 0x33333333u);

    CHECK(bus.dma_interrupt() == 0u);
    const auto dicr_reset = bus.read32(0x1F8010F4u);
    CHECK(dicr_reset.status == jojo::R3000aBusStatus::ok);
    CHECK(dicr_reset.value == 0u);
    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F8010F4u, 0x00FF807Fu).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    const auto dicr_written = bus.read32(0x1F8010F4u);
    CHECK(dicr_written.status == jojo::R3000aBusStatus::ok);
    CHECK(dicr_written.value == 0x80FF807Fu);
    CHECK(bus.dma_interrupt() == 0x80FF807Fu);
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F8010F4u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.read32(0x1F8010F4u).value == 0u);
    bus.set_diagnostic_mmio_probe_enabled(false);

    CHECK(bus.write32(0x1F801114u, 0xABCD0100u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.timer1_mode() == 0x0100u);
    CHECK(bus.timer1_counter() == 0x0000u);
    CHECK(bus.read32(0x1F801114u).status == jojo::R3000aBusStatus::unsupported);

    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();
    const auto timer1_counter = bus.read32(0x1F801110u);
    CHECK(timer1_counter.status == jojo::R3000aBusStatus::ok);
    CHECK(timer1_counter.value == 0x00000000u);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    bus.set_diagnostic_mmio_probe_enabled(false);

    bus.clear_last_unsupported_access();
    bus.set_diagnostic_mmio_probe_enabled(true);
    const auto probe_zero = bus.read32(0x1F801080u);
    CHECK(probe_zero.status == jojo::R3000aBusStatus::ok);
    CHECK(probe_zero.value == 0u);
    CHECK(bus.last_diagnostic_mmio_probe().has_value());
    if (bus.last_diagnostic_mmio_probe()) {
        CHECK(bus.last_diagnostic_mmio_probe()->guest_address == 0x1F801080u);
        CHECK(bus.last_diagnostic_mmio_probe()->width == 4u);
        CHECK(!bus.last_diagnostic_mmio_probe()->write);
        CHECK(bus.last_diagnostic_mmio_probe()->value == 0u);
    }

    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F801080u, 0x12345678u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.last_diagnostic_mmio_probe().has_value());
    if (bus.last_diagnostic_mmio_probe()) {
        CHECK(bus.last_diagnostic_mmio_probe()->write);
        CHECK(bus.last_diagnostic_mmio_probe()->value == 0x12345678u);
    }
    bus.clear_last_diagnostic_mmio_probe();
    const auto probe_shadow = bus.read32(0x1F801080u);
    CHECK(probe_shadow.status == jojo::R3000aBusStatus::ok);
    CHECK(probe_shadow.value == 0x12345678u);

    bus.set_diagnostic_mmio_probe_enabled(false);
    CHECK(bus.read32(0x1F801080u).status == jojo::R3000aBusStatus::unsupported);

    CHECK(bus.read8(0x00200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read8(0x80200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read8(0xA0200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read32(0xC0000000u).status == jojo::R3000aBusStatus::unsupported);

    {
        jojo::Ps1MemoryBus cd_bus;
        cd_bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
        cd_bus.set_diagnostic_mmio_probe_enabled(true);

        cd_bus.clear_last_diagnostic_mmio_probe();
        CHECK(cd_bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
        CHECK(!cd_bus.last_diagnostic_mmio_probe());
        const auto hintsts = cd_bus.read8(0x1F801803u);
        CHECK(hintsts.status == jojo::R3000aBusStatus::ok);
        CHECK(hintsts.value == 0xE0u);
        CHECK(!cd_bus.last_diagnostic_mmio_probe());

        CHECK(cd_bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
        CHECK(cd_bus.write8(0x1F801803u, 0x00u).status == jojo::R3000aBusStatus::ok);
        CHECK(cd_bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
        CHECK(cd_bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
        CHECK(cd_bus.interrupt_status() == 0x0004u);
        CHECK(!cd_bus.last_diagnostic_mmio_probe());

        const auto istat16 = cd_bus.read16(0x1F801070u);
        CHECK(istat16.status == jojo::R3000aBusStatus::ok);
        CHECK(istat16.value == 0x0004u);
        const auto istat32 = cd_bus.read32(0x1F801070u);
        CHECK(istat32.status == jojo::R3000aBusStatus::ok);
        CHECK(istat32.value == 0x00000004u);
        CHECK(!cd_bus.last_diagnostic_mmio_probe());

        CHECK(cd_bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
        CHECK(cd_bus.interrupt_status() == 0u);
        CHECK(cd_bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
        CHECK(cd_bus.interrupt_status() == 0u);

        cd_bus.clear_last_unsupported_cdrom_command();
        CHECK(cd_bus.write8(0x1F801801u, 0x02u).status == jojo::R3000aBusStatus::unsupported);
        CHECK(cd_bus.last_unsupported_cdrom_command().has_value());
        CHECK(cd_bus.read16(0x1F801800u).status == jojo::R3000aBusStatus::unsupported);
        CHECK(cd_bus.read32(0x1F801800u).status == jojo::R3000aBusStatus::unsupported);
    }

    {
        jojo::Ps1MemoryBus left;
        jojo::Ps1MemoryBus right;
        for (auto* candidate : {&left, &right}) {
            candidate->cdrom().seed_post_bios(0x02u, 0x1Fu);
            CHECK(candidate->write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
            CHECK(candidate->write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
        }
        CHECK(left.diagnostic_state_hash() == right.diagnostic_state_hash());
    }

    {
        jojo::Ps1MemoryBus override_bus;
        const jojo::Ps1UnsupportedAccess byte_access{
            0x1F801802u, 0x1F801802u, 1u, false, 0u,
        };
        CHECK(override_bus.arm_diagnostic_mmio_read_override(byte_access, 0x12345680u));
        CHECK(override_bus.diagnostic_mmio_read_override().has_value());

        const auto supported_istat = override_bus.read32(0x1F801070u);
        CHECK(supported_istat.status == jojo::R3000aBusStatus::ok);
        CHECK(supported_istat.value == 0u);
        CHECK(override_bus.diagnostic_mmio_read_override().has_value());

        const auto wrong_width = override_bus.read16(0x1F801802u);
        CHECK(wrong_width.status == jojo::R3000aBusStatus::unsupported);
        CHECK(override_bus.diagnostic_mmio_read_override().has_value());

        const auto first = override_bus.read8(0x1F801802u);
        CHECK(first.status == jojo::R3000aBusStatus::ok);
        CHECK(first.value == 0x80u);
        CHECK(!override_bus.diagnostic_mmio_read_override().has_value());
        CHECK(override_bus.read8(0x1F801802u).status == jojo::R3000aBusStatus::unsupported);

        const jojo::Ps1UnsupportedAccess half_access{
            0x1F801800u, 0x1F801800u, 2u, false, 0u,
        };
        CHECK(override_bus.arm_diagnostic_mmio_read_override(half_access, 0x12348001u));
        const auto half = override_bus.read16(0x1F801800u);
        CHECK(half.status == jojo::R3000aBusStatus::ok);
        CHECK(half.value == 0x8001u);

        const jojo::Ps1UnsupportedAccess word_access{
            0x1F801800u, 0x1F801800u, 4u, false, 0u,
        };
        CHECK(override_bus.arm_diagnostic_mmio_read_override(word_access, 0x89ABCDEFu));
        const auto word = override_bus.read32(0x1F801800u);
        CHECK(word.status == jojo::R3000aBusStatus::ok);
        CHECK(word.value == 0x89ABCDEFu);

        auto write_access = byte_access;
        write_access.write = true;
        CHECK(!override_bus.arm_diagnostic_mmio_read_override(write_access, 0u));
        auto invalid_width = byte_access;
        invalid_width.width = 3u;
        CHECK(!override_bus.arm_diagnostic_mmio_read_override(invalid_width, 0u));
    }

    {
        jojo::Ps1MemoryBus zero_bus;
        jojo::Ps1MemoryBus one_bus;
        CHECK(zero_bus.diagnostic_state_hash() == one_bus.diagnostic_state_hash());
        const jojo::Ps1UnsupportedAccess access{
            0x1F801802u, 0x1F801802u, 1u, false, 0u,
        };
        CHECK(zero_bus.arm_diagnostic_mmio_read_override(access, 0u));
        CHECK(one_bus.arm_diagnostic_mmio_read_override(access, 1u));
        CHECK(zero_bus.diagnostic_state_hash() != one_bus.diagnostic_state_hash());
        CHECK(zero_bus.read8(0x1F801802u).status == jojo::R3000aBusStatus::ok);
        CHECK(one_bus.read8(0x1F801802u).status == jojo::R3000aBusStatus::ok);
        CHECK(zero_bus.diagnostic_state_hash() == one_bus.diagnostic_state_hash());
    }

    return failures ? 1 : 0;
}
