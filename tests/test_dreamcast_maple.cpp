#include "core/dreamcast_maple.h"
#include "core/input.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

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
        if (!read) return jojo::Result<std::uint32_t>::failure(read.error, read.detail);
        value |= static_cast<std::uint32_t>(read.value) << (byte * 8u);
    }
    return jojo::Result<std::uint32_t>::success(value);
}

static std::uint32_t maple_request_header(std::uint8_t command,
                                          std::uint8_t port,
                                          std::uint8_t payload_words) {
    const auto destination = static_cast<std::uint8_t>(0x20u + port * 0x40u);
    const auto source = static_cast<std::uint8_t>(port * 0x40u);
    return (static_cast<std::uint32_t>(command) << 24u) |
           (static_cast<std::uint32_t>(destination) << 16u) |
           (static_cast<std::uint32_t>(source) << 8u) |
           payload_words;
}

static std::uint32_t prepare_entry(jojo::DreamcastExecutableMemory& memory,
                                   std::uint32_t table,
                                   bool final_entry,
                                   std::uint8_t port,
                                   std::uint32_t receive,
                                   std::uint8_t command,
                                   const std::vector<std::uint32_t>& payload = {}) {
    const auto control = (final_entry ? 0x80000000u : 0u) |
                         (static_cast<std::uint32_t>(port & 3u) << 16u) |
                         static_cast<std::uint32_t>(payload.size());
    CHECK(jojo::write_dreamcast_u32(memory, table + 0u, control));
    CHECK(jojo::write_dreamcast_u32(memory, table + 4u, receive));
    CHECK(jojo::write_dreamcast_u32(
        memory, table + 8u,
        maple_request_header(command, port, static_cast<std::uint8_t>(payload.size()))));
    for (std::size_t i = 0; i < payload.size(); ++i) {
        CHECK(jojo::write_dreamcast_u32(
            memory, table + 12u + static_cast<std::uint32_t>(i * 4u), payload[i]));
    }
    return table + 12u + static_cast<std::uint32_t>(payload.size() * 4u);
}

static std::uint32_t read_word(const jojo::DreamcastExecutableMemory& memory,
                               std::uint32_t address) {
    const auto result = jojo::read_dreamcast_u32(memory, address);
    CHECK(result);
    return result ? result.value : 0u;
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

static jojo::Result<void> start_dma(jojo::DreamcastMaple& maple,
                                    std::uint32_t table) {
    const auto star = write_mmio32(maple, 0xA05F6C04u, table);
    if (!star) return star;
    const auto enable = write_mmio32(maple, 0xA05F6C14u, 1u);
    if (!enable) return enable;
    return write_mmio32(maple, 0xA05F6C18u, 1u);
}

static void test_device_request_returns_controller_info() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000400u;
    prepare_entry(memory, table, true, 0u, receive, 0x01u);

    CHECK(start_dma(maple, table));
    CHECK(read_word(memory, receive + 0u) == 0x0500201Cu);
    CHECK(read_word(memory, receive + 4u) == 0x00000001u);
    CHECK(read_word(memory, receive + 8u) == 0xFE060F00u);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) != 0u);
}

static void test_multi_entry_chain_processes_consecutive_descriptors() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C001000u;
    constexpr std::uint32_t receive0 = 0x0C002000u;
    constexpr std::uint32_t receive1 = 0x0C002100u;

    auto next = prepare_entry(memory, table, false, 0u, receive0, 0x01u);
    CHECK(next == table + 12u);
    prepare_entry(memory, next, true, 1u, receive1, 0x01u);

    CHECK(start_dma(maple, table));
    CHECK(read_word(memory, receive0) == 0x0500201Cu);
    CHECK(read_word(memory, receive1) == 0x0540601Cu);
    CHECK((read_normal_status(asic) & (1u << 12u)) != 0u);
}

static void test_validation_failure_is_atomic_across_chain() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C003000u;
    constexpr std::uint32_t receive0 = 0x0C004000u;
    constexpr std::uint32_t receive1 = 0x0C004100u;
    CHECK(jojo::write_dreamcast_u32(memory, receive0, 0x11111111u));
    CHECK(jojo::write_dreamcast_u32(memory, receive1, 0x22222222u));

    const auto next = prepare_entry(memory, table, false, 0u, receive0, 0x01u);
    prepare_entry(memory, next, true, 1u, receive1 + 2u, 0x01u);

    const auto start = start_dma(maple, table);
    CHECK(!start);
    CHECK(read_word(memory, receive0) == 0x11111111u);
    CHECK(read_word(memory, receive1) == 0x22222222u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_absent_port_returns_no_device_without_failing_dma() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C005000u;
    constexpr std::uint32_t receive = 0x0C005800u;
    prepare_entry(memory, table, true, 2u, receive, 0x01u);

    CHECK(start_dma(maple, table));
    CHECK(read_word(memory, receive) == 0xFFFFFFFFu);
    CHECK((read_normal_status(asic) & (1u << 12u)) != 0u);
}

template <typename Maple>
static void test_get_condition_bridges_existing_input_model_impl() {
    if constexpr (std::is_constructible_v<
                      Maple, jojo::DreamcastExecutableMemory&,
                      jojo::DreamcastSystemAsic&,
                      const jojo::ResolvedInputFrame&>) {
        auto memory = make_memory();
        jojo::DreamcastSystemAsic asic;
        jojo::ResolvedInputFrame input{};
        input[0].actions[jojo::GameAction::left] = true;
        input[0].actions[jojo::GameAction::attack_light] = true;
        input[0].actions[jojo::GameAction::attack_medium] = true;
        input[0].actions[jojo::GameAction::attack_heavy] = true;
        input[0].actions[jojo::GameAction::stand] = true;
        input[0].actions[jojo::GameAction::start] = true;
        Maple maple(memory, asic, input);

        constexpr std::uint32_t table = 0x0C006000u;
        constexpr std::uint32_t receive = 0x0C006800u;
        prepare_entry(memory, table, true, 0u, receive, 0x09u, {0x00000001u});
        CHECK(start_dma(maple, table));

        CHECK(read_word(memory, receive + 0u) == 0x08002003u);
        CHECK(read_word(memory, receive + 4u) == 0x00000001u);
        // X/Y/B/A/Start/Left pressed; raw Maple buttons are active-low.
        CHECK(read_word(memory, receive + 8u) == 0xF9B10000u);
        // Right stick neutral; left stick centered vertically and fully left.
        CHECK(read_word(memory, receive + 12u) == 0x00808080u);
    } else {
        CHECK(false && "DreamcastMaple must accept the existing ResolvedInputFrame bridge");
    }
}

static void test_get_condition_bridges_existing_input_model() {
    test_get_condition_bridges_existing_input_model_impl<jojo::DreamcastMaple>();
}

static void test_disabled_dma_rejects_start_and_stays_idle() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C007000u;
    constexpr std::uint32_t receive = 0x0C007800u;
    prepare_entry(memory, table, true, 0u, receive, 0x01u);
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0x12345678u));
    CHECK(write_mmio32(maple, 0xA05F6C04u, table));

    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);
    CHECK(read_word(memory, receive) == 0x12345678u);
    const auto mdst = read_mmio32(maple, 0xA05F6C18u);
    CHECK(mdst);
    if (mdst) CHECK(mdst.value == 0u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_hardware_trigger_mode_is_rejected() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C008000u;
    constexpr std::uint32_t receive = 0x0C008800u;
    prepare_entry(memory, table, true, 0u, receive, 0x01u);
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0x89ABCDEFu));
    CHECK(write_mmio32(maple, 0xA05F6C04u, table));
    CHECK(write_mmio32(maple, 0xA05F6C10u, 1u));
    CHECK(write_mmio32(maple, 0xA05F6C14u, 1u));
    const auto start = write_mmio32(maple, 0xA05F6C18u, 1u);
    CHECK(!start);
    CHECK(read_word(memory, receive) == 0x89ABCDEFu);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_frame_length_mismatch_is_rejected_without_write() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C009000u;
    constexpr std::uint32_t receive = 0x0C009800u;
    CHECK(jojo::write_dreamcast_u32(memory, table + 0u, 0x80000000u));
    CHECK(jojo::write_dreamcast_u32(memory, table + 4u, receive));
    CHECK(jojo::write_dreamcast_u32(memory, table + 8u, 0x01200001u));
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0x13579BDFu));

    const auto start = start_dma(maple, table);
    CHECK(!start);
    CHECK(read_word(memory, receive) == 0x13579BDFu);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_payload_span_crossing_maple_ram_area_is_rejected_atomically() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0FFFFFE0u;
    constexpr std::uint32_t receive = 0x0C00A000u;
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0xDEADBEEFu));
    CHECK(jojo::write_dreamcast_u32(memory, table + 0u, 0x80000008u));
    CHECK(jojo::write_dreamcast_u32(memory, table + 4u, receive));
    CHECK(jojo::write_dreamcast_u32(memory, table + 8u, 0x01200008u));
    for (std::uint32_t i = 0u; i < 5u; ++i) {
        CHECK(jojo::write_dreamcast_u32(memory, table + 12u + i * 4u, 0x00000001u));
    }

    const auto start = start_dma(maple, table);
    CHECK(!start);
    CHECK(read_word(memory, receive) == 0xDEADBEEFu);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

static void test_unterminated_chain_hits_runtime_safety_limit_without_side_effects() {
    auto memory = make_memory();
    jojo::DreamcastSystemAsic asic;
    jojo::DreamcastMaple maple(memory, asic);
    constexpr std::uint32_t table = 0x0C010000u;
    constexpr std::uint32_t receive = 0x0C020000u;
    CHECK(jojo::write_dreamcast_u32(memory, receive, 0xA5A5A5A5u));
    auto cursor = table;
    for (std::uint32_t i = 0u; i < 257u; ++i) {
        cursor = prepare_entry(memory, cursor, false,
                               static_cast<std::uint8_t>(i & 1u), receive, 0x01u);
    }

    const auto start = start_dma(maple, table);
    CHECK(!start);
    CHECK(read_word(memory, receive) == 0xA5A5A5A5u);
    CHECK((read_normal_status(asic) & (1u << 12u)) == 0u);
}

int main() {
    test_device_request_returns_controller_info();
    test_multi_entry_chain_processes_consecutive_descriptors();
    test_validation_failure_is_atomic_across_chain();
    test_absent_port_returns_no_device_without_failing_dma();
    test_get_condition_bridges_existing_input_model();
    test_disabled_dma_rejects_start_and_stays_idle();
    test_hardware_trigger_mode_is_rejected();
    test_frame_length_mismatch_is_rejected_without_write();
    test_payload_span_crossing_maple_ram_area_is_rejected_atomically();
    test_unterminated_chain_hits_runtime_safety_limit_without_side_effects();

    if (failures) {
        std::cerr << failures << " Dreamcast Maple assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast Maple R2.3 assertions passed\n";
    return 0;
}
