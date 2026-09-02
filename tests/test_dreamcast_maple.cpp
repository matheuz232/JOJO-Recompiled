#include "core/dreamcast_maple.h"

#include <cstdint>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::DreamcastExecutableMemory make_memory() {
    jojo::DreamcastExecutableMemory memory{};
    memory.main_ram.resize(jojo::kDreamcastMainRamSize, 0u);
    return memory;
}

static jojo::Result<void> write_mmio32(jojo::DreamcastMmioDevice& device,
                                       std::uint32_t address,
                                       std::uint32_t value) {
    for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
        const auto write = device.write8(
            address + byte,
            static_cast<std::uint8_t>((value >> (byte * 8u)) & 0xFFu));
        if (!write) return write;
    }
    return jojo::Result<void>::success();
}

static jojo::Result<std::uint32_t> read_mmio32(jojo::DreamcastMmioDevice& device,
                                               std::uint32_t address) {
    std::uint32_t value = 0u;
    for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
        const auto read = device.read8(address + byte);
        if (!read) {
            return jojo::Result<std::uint32_t>::failure(read.error, read.detail);
        }
        value |= static_cast<std::uint32_t>(read.value) << (byte * 8u);
    }
    return jojo::Result<std::uint32_t>::success(value);
}

static void prepare_single_entry(jojo::DreamcastExecutableMemory& memory,
                                 std::uint32_t table,
                                 std::uint32_t receive,
                                 std::uint32_t control = 0x80000000u,
                                 std::uint32_t frame_header = 0x01200000u) {
    CHECK(jojo::write_dreamcast_u32(memory, table + 0u, control));
    CHECK(jojo::write_dreamcast_u32(memory, table + 4u, receive));
    CHECK(jojo::write_dreamcast_u32(memory, table + 8u, frame_header));
}

static std::uint32_t read_normal_status(jojo::DreamcastSystemAsic& asic) {
    std::uint32_t value = 0u;
    for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
        const auto read = asic.read8(0x005F6900u + byte);
        CHECK(read);
        if (read) value |= static_cast<std::uint32_t>(read.value) << (byte * 8u);
    }
    return value;
}

static void test_successful_single_entry_dma_returns_no_device_and_idle() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;
    prepare_single_entry(memory, table, receive);

    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    CHECK(write_mmio32(maple, 0xA05F6C18u, 1u));

    const auto response = jojo::read_dreamcast_u32(memory, receive);
    CHECK(response);
    if (response) CHECK(response.value == 0xFFFFFFFFu);

    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
}

static void test_disabled_dma_rejects_start_and_stays_idle() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;
    prepare_single_entry(memory, table, receive);
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0x12345678u));
    CHECK(write_mmio32(maple, 0xA05F6C04u, table));

    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);

    const auto response = jojo::read_dreamcast_u32(memory, receive);
    CHECK(response);
    if (response) CHECK(response.value == 0x12345678u);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_hardware_trigger_mode_is_rejected() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;
    prepare_single_entry(memory, table, receive);
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0x89ABCDEFu));

    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C10u, 1u));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);

    const auto response = jojo::read_dreamcast_u32(memory, receive);
    CHECK(response);
    if (response) CHECK(response.value == 0x89ABCDEFu);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_nonfinal_descriptor_is_rejected_and_stays_idle() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;
    prepare_single_entry(memory, table, receive, 0x00000000u);
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0xCAFEBABEu));

    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);

    const auto response = jojo::read_dreamcast_u32(memory, receive);
    CHECK(response);
    if (response) CHECK(response.value == 0xCAFEBABEu);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_frame_length_mismatch_is_rejected() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;
    prepare_single_entry(memory,
                         table,
                         receive,
                         0x80000000u,
                         0x01200001u); // header claims one payload word, descriptor claims zero
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0x13579BDFu));

    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);

    const auto response = jojo::read_dreamcast_u32(memory, receive);
    CHECK(response);
    if (response) CHECK(response.value == 0x13579BDFu);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_misaligned_receive_address_is_rejected() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t sentinel = 0x0C000200u;
    prepare_single_entry(memory, table, sentinel + 2u);
    CHECK(jojo::write_dreamcast_u32(memory, sentinel, 0x0BADF00Du));

    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);

    const auto untouched = jojo::read_dreamcast_u32(memory, sentinel);
    CHECK(untouched);
    if (untouched) CHECK(untouched.value == 0x0BADF00Du);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_successful_dma_raises_maple_completion_interrupt() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;
    prepare_single_entry(memory, table, receive);

    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    CHECK(write_mmio32(maple, 0xA05F6C18u, 1u));

    CHECK((read_normal_status(asic) & (1u << 12u)) != 0u);
}

int main() {
    test_successful_single_entry_dma_returns_no_device_and_idle();
    test_disabled_dma_rejects_start_and_stays_idle();
    test_hardware_trigger_mode_is_rejected();
    test_nonfinal_descriptor_is_rejected_and_stays_idle();
    test_frame_length_mismatch_is_rejected();
    test_misaligned_receive_address_is_rejected();
    test_successful_dma_raises_maple_completion_interrupt();

    if (failures) {
        std::cerr << failures << " Dreamcast Maple assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast Maple assertions passed\n";
    return 0;
}
