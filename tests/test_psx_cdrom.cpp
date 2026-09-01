#include "core/psx_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

namespace {

constexpr std::uint32_t cd = jojo::PsxBus::cdrom_base_address;

void select_bank(jojo::PsxBus& bus, std::uint8_t bank) {
    CHECK(jojo::psx_bus_write_u8(bus, cd, bank) == jojo::PsxBusAccessReason::ok);
}

void acknowledge(jojo::PsxBus& bus) {
    select_bank(bus, 1u);
    CHECK(jojo::psx_bus_write_u8(bus, cd + 3u, 0x1fu) == jojo::PsxBusAccessReason::ok);
    select_bank(bus, 0u);
}

std::uint8_t read_result(jojo::PsxBus& bus) {
    const auto result = jojo::psx_bus_read_u8(bus, cd + 1u);
    CHECK(result.reason == jojo::PsxBusAccessReason::ok);
    return result.value;
}

std::uint8_t irq_flags(jojo::PsxBus& bus) {
    select_bank(bus, 1u);
    const auto value = jojo::psx_bus_read_u8(bus, cd + 3u);
    CHECK(value.reason == jojo::PsxBusAccessReason::ok);
    select_bank(bus, 0u);
    return static_cast<std::uint8_t>(value.value & jojo::PsxBus::cdrom_interrupt_bits);
}

void send_parameter(jojo::PsxBus& bus, std::uint8_t value) {
    select_bank(bus, 0u);
    CHECK(jojo::psx_bus_write_u8(bus, cd + 2u, value) == jojo::PsxBusAccessReason::ok);
}

void send_command(jojo::PsxBus& bus, std::uint8_t command) {
    select_bank(bus, 0u);
    CHECK(jojo::psx_bus_write_u8(bus, cd + 1u, command) == jojo::PsxBusAccessReason::ok);
}

void test_parameter_fifo_and_setloc() {
    jojo::PsxBus bus{};
    send_parameter(bus, 0x00u);
    send_parameter(bus, 0x02u);
    send_parameter(bus, 0x00u);

    const auto before = jojo::psx_bus_read_u8(bus, cd);
    CHECK(before.reason == jojo::PsxBusAccessReason::ok);
    CHECK((before.value & jojo::PsxBus::cdrom_hsts_parameter_empty) == 0u);
    CHECK((before.value & jojo::PsxBus::cdrom_hsts_parameter_write_ready) != 0u);

    send_command(bus, 0x02u); // Setloc 00:02:00 = logical LBA 0.
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);

    const auto after = jojo::psx_bus_read_u8(bus, cd);
    CHECK((after.value & jojo::PsxBus::cdrom_hsts_parameter_empty) != 0u);
    acknowledge(bus);
}

void test_setmode_setfilter_and_getparam() {
    jojo::PsxBus bus{};

    send_parameter(bus, 0x80u);
    send_command(bus, 0x0eu); // Setmode
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(bus);

    send_parameter(bus, 0x11u);
    send_parameter(bus, 0x22u);
    send_command(bus, 0x0du); // Setfilter
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(bus);

    send_command(bus, 0x0fu); // Getparam
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    CHECK(read_result(bus) == 0x80u);
    CHECK(read_result(bus) == 0x00u);
    CHECK(read_result(bus) == 0x11u);
    CHECK(read_result(bus) == 0x22u);
    acknowledge(bus);
}

void test_pause_accepts_empty_parameter_fifo() {
    jojo::PsxBus bus{};
    send_command(bus, 0x09u); // Pause: first response is INT3(status).
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(bus);
}

} // namespace

int main() {
    test_parameter_fifo_and_setloc();
    test_setmode_setfilter_and_getparam();
    test_pause_accepts_empty_parameter_fifo();
    if (failures != 0) return 1;
    std::cout << "PS1 CD-ROM command foundation assertions passed\n";
    return 0;
}
