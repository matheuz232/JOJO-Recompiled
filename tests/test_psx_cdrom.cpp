#include "core/psx_runtime.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

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

    send_command(bus, 0x02u);
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);

    const auto after = jojo::psx_bus_read_u8(bus, cd);
    CHECK((after.value & jojo::PsxBus::cdrom_hsts_parameter_empty) != 0u);
    acknowledge(bus);
}

void test_setmode_setfilter_and_getparam() {
    jojo::PsxBus bus{};

    send_parameter(bus, 0x80u);
    send_command(bus, 0x0eu);
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(bus);

    send_parameter(bus, 0x11u);
    send_parameter(bus, 0x22u);
    send_command(bus, 0x0du);
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(bus);

    send_command(bus, 0x0fu);
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
    send_command(bus, 0x09u);
    CHECK(irq_flags(bus) == 0x03u);
    CHECK(read_result(bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(bus);
}

void test_readn_streams_prepared_2048_byte_sectors() {
    const auto disc_path = std::filesystem::current_path() / "jojo_psx_cdrom_contract_disc.iso";
    std::error_code ec;
    std::filesystem::remove(disc_path, ec);

    std::vector<std::uint8_t> image(4096u);
    for (std::size_t i = 0; i < 2048u; ++i) {
        image[i] = static_cast<std::uint8_t>(i & 0xffu);
        image[2048u + i] = static_cast<std::uint8_t>(0x80u | (i & 0x7fu));
    }
    {
        std::ofstream out(disc_path, std::ios::binary | std::ios::trunc);
        CHECK(static_cast<bool>(out));
        out.write(reinterpret_cast<const char*>(image.data()),
                  static_cast<std::streamsize>(image.size()));
        CHECK(static_cast<bool>(out));
    }

    jojo::PsxRuntime runtime{};
    const auto mounted = jojo::mount_psx_cdrom_image(runtime, disc_path);
    CHECK(static_cast<bool>(mounted));

    send_parameter(runtime.bus, 0x00u);
    send_parameter(runtime.bus, 0x02u);
    send_parameter(runtime.bus, 0x00u);
    send_command(runtime.bus, 0x02u);
    CHECK(irq_flags(runtime.bus) == 0x03u);
    CHECK(read_result(runtime.bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(runtime.bus);

    send_parameter(runtime.bus, 0x80u);
    send_command(runtime.bus, 0x0eu);
    CHECK(irq_flags(runtime.bus) == 0x03u);
    CHECK(read_result(runtime.bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(runtime.bus);

    send_command(runtime.bus, 0x06u);
    CHECK(irq_flags(runtime.bus) == 0x03u);
    CHECK(read_result(runtime.bus) == jojo::PsxBus::cdrom_status_motor_on);
    acknowledge(runtime.bus);

    jojo::service_psx_cdrom(runtime);
    CHECK(irq_flags(runtime.bus) == 0x01u);
    CHECK(read_result(runtime.bus) == jojo::PsxBus::cdrom_status_motor_on);

    auto hsts = jojo::psx_bus_read_u8(runtime.bus, cd);
    CHECK((hsts.value & jojo::PsxBus::cdrom_hsts_data_request) == 0u);
    select_bank(runtime.bus, 0u);
    CHECK(jojo::psx_bus_write_u8(runtime.bus, cd + 3u, 0x80u) ==
          jojo::PsxBusAccessReason::ok);
    hsts = jojo::psx_bus_read_u8(runtime.bus, cd);
    CHECK((hsts.value & jojo::PsxBus::cdrom_hsts_data_request) != 0u);

    for (std::size_t i = 0; i < 2048u; ++i) {
        const auto byte = jojo::psx_bus_read_u8(runtime.bus, cd + 2u);
        CHECK(byte.reason == jojo::PsxBusAccessReason::ok);
        CHECK(byte.value == static_cast<std::uint8_t>(i & 0xffu));
    }
    hsts = jojo::psx_bus_read_u8(runtime.bus, cd);
    CHECK((hsts.value & jojo::PsxBus::cdrom_hsts_data_request) == 0u);
    acknowledge(runtime.bus);

    jojo::service_psx_cdrom(runtime);
    CHECK(irq_flags(runtime.bus) == 0x01u);
    CHECK(read_result(runtime.bus) == jojo::PsxBus::cdrom_status_motor_on);
    select_bank(runtime.bus, 0u);
    CHECK(jojo::psx_bus_write_u8(runtime.bus, cd + 3u, 0x80u) ==
          jojo::PsxBusAccessReason::ok);
    for (std::size_t i = 0; i < 16u; ++i) {
        const auto byte = jojo::psx_bus_read_u8(runtime.bus, cd + 2u);
        CHECK(byte.reason == jojo::PsxBusAccessReason::ok);
        CHECK(byte.value == static_cast<std::uint8_t>(0x80u | (i & 0x7fu)));
    }

    std::filesystem::remove(disc_path, ec);
}

} // namespace

int main() {
    test_parameter_fifo_and_setloc();
    test_setmode_setfilter_and_getparam();
    test_pause_accepts_empty_parameter_fifo();
    test_readn_streams_prepared_2048_byte_sectors();
    if (failures != 0) return 1;
    std::cout << "PS1 CD-ROM command and streaming assertions passed\n";
    return 0;
}
