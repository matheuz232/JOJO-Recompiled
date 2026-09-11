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

static void prepare_getstat_irq(jojo::Ps1CdromState& cdrom) {
    cdrom.seed_post_bios(0x02u, 0x1Fu);
    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.interrupt_status() == 3u);
    CHECK(cdrom.irq_line());
}

static void test_hsts_reflects_index_and_result_fifo() {
    jojo::Ps1CdromState cdrom;
    cdrom.seed_post_bios(0x02u, 0x1Fu);

    auto hsts = cdrom.read8(0x1F801800u);
    CHECK(hsts.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hsts.value == 0x18u);

    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    hsts = cdrom.read8(0x1F801800u);
    CHECK(hsts.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hsts.value == 0x19u);

    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    hsts = cdrom.read8(0x1F801800u);
    CHECK(hsts.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hsts.value == 0x38u);

    const auto result = cdrom.read8(0x1F801801u);
    CHECK(result.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(result.value == 0x02u);
    hsts = cdrom.read8(0x1F801800u);
    CHECK(hsts.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hsts.value == 0x18u);
}

static void test_result_fifo_is_readable_in_bank1() {
    jojo::Ps1CdromState cdrom;
    cdrom.seed_post_bios(0x02u, 0x1Fu);

    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.read8(0x1F801800u).value == 0x39u);

    const auto result = cdrom.read8(0x1F801801u);
    CHECK(result.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(result.value == 0x02u);
    CHECK(cdrom.read8(0x1F801800u).value == 0x19u);
}

static void test_bank1_hclrctl_acknowledges_getstat_irq() {
    jojo::Ps1CdromState cdrom;
    prepare_getstat_irq(cdrom);

    const auto response = cdrom.read8(0x1F801801u);
    CHECK(response.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(response.value == 0x02u);

    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    const auto before_ack = cdrom.diagnostic_state_hash();
    const auto ack = cdrom.write8(0x1F801803u, 0x07u);

    CHECK(ack.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.interrupt_status() == 0u);
    CHECK(!cdrom.irq_line());
    CHECK(cdrom.diagnostic_state_hash() != before_ack);
}

static void test_bank1_hclrctl_drains_result_fifo() {
    jojo::Ps1CdromState cdrom;
    prepare_getstat_irq(cdrom);
    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801803u, 0x07u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.read8(0x1F801801u).status == jojo::Ps1CdromIoStatus::unsupported_register);
}

static void test_bank1_hclrctl_rejects_unmodeled_side_effect_bits() {
    for (const std::uint8_t value : {0x20u, 0x40u, 0x80u, 0x27u, 0x47u, 0x87u}) {
        jojo::Ps1CdromState cdrom;
        prepare_getstat_irq(cdrom);
        CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);

        const auto before = cdrom.diagnostic_state_hash();
        const auto result = cdrom.write8(0x1F801803u, value);

        CHECK(result.status == jojo::Ps1CdromIoStatus::unsupported_register);
        CHECK(cdrom.diagnostic_state_hash() == before);
        CHECK(cdrom.interrupt_status() == 3u);
        CHECK(cdrom.irq_line());
    }
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

    CHECK(first.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::unsupported_command);
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

    jojo::Ps1CdromState bank1;
    bank1.seed_post_bios(0x02u, 0x1Fu);
    CHECK(bank1.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(bank1.write8(0x1F801803u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);

    test_hsts_reflects_index_and_result_fifo();
    test_result_fifo_is_readable_in_bank1();
    test_bank1_hclrctl_acknowledges_getstat_irq();
    test_bank1_hclrctl_drains_result_fifo();
    test_bank1_hclrctl_rejects_unmodeled_side_effect_bits();
    return failures ? 1 : 0;
}
