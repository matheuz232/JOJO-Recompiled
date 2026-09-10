#include "core/ps1_cdrom_state.h"
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void run_observed_sequence(jojo::Ps1CdromState& cdrom) {
    cdrom.seed_post_bios(0x02u, 0x1Fu);
    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    const auto hintsts = cdrom.read8(0x1F801803u);
    CHECK(hintsts.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hintsts.value == 0xE0u);
    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801803u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
}

int main() {
    jojo::Ps1CdromState first;
    CHECK(first.index() == 0u);
    CHECK(first.drive_status() == 0u);
    CHECK(first.interrupt_enable() == 0u);
    CHECK(first.interrupt_status() == 0u);
    CHECK(first.command_count() == 0u);
    CHECK(!first.irq_line());

    run_observed_sequence(first);
    CHECK(first.drive_status() == 0x02u);
    CHECK(first.interrupt_enable() == 0x1Fu);
    CHECK(first.command_count() == 1u);
    CHECK(first.interrupt_status() == 3u);
    CHECK(first.irq_line());
    CHECK(first.take_irq_rising_edge());
    CHECK(!first.take_irq_rising_edge());

    const auto& event = first.last_command_event();
    CHECK(event.has_value());
    if (event) {
        CHECK(event->sequence == 1u);
        CHECK(event->command == 0x01u);
        CHECK(event->index == 0u);
        CHECK(event->status == 0x02u);
    }

    const auto result = first.read8(0x1F801801u);
    CHECK(result.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(result.value == 0x02u);
    CHECK(first.read8(0x1F801801u).status == jojo::Ps1CdromIoStatus::unsupported_register);

    jojo::Ps1CdromState left;
    jojo::Ps1CdromState right;
    run_observed_sequence(left);
    run_observed_sequence(right);
    CHECK(left.diagnostic_state_hash() == right.diagnostic_state_hash());

    CHECK(left.write8(0x1F801801u, 0x02u).status == jojo::Ps1CdromIoStatus::unsupported_command);
    CHECK(left.write8(0x1F801803u, 0x01u).status == jojo::Ps1CdromIoStatus::unsupported_register);
    CHECK(left.write8(0x1F801800u, 0x03u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(left.read8(0x1F801803u).status == jojo::Ps1CdromIoStatus::unsupported_register);
    return failures ? 1 : 0;
}
