#include "core/dreamcast_boot_runner.h"
#include "core/input.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(jojo::DreamcastBootProgram& program, std::uint16_t word) {
    program.bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    program.bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static void test_executes_loaded_boot_program_from_main_ram() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0xE105u);
    append_word(program, 0x7103u);
    const auto run = jojo::run_dreamcast_boot_reference(program, {}, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(run.value.state.r[1] == 8u);
    CHECK(run.value.state.pc == jojo::kDreamcastBootLoadAddress + 4u);
    CHECK(run.value.blocks_executed == 1u);
    CHECK(run.value.operations_executed == 2u);
    CHECK(!run.value.unsupported_address.has_value());
    CHECK(!run.value.unsupported_raw.has_value());
}

static void test_reports_first_reachable_unsupported_opcode() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x0009u);
    append_word(program, 0xFFFFu);
    const auto run = jojo::run_dreamcast_boot_reference(program, {}, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::unsupported_opcode);
    CHECK(run.value.unsupported_address.has_value());
    CHECK(run.value.unsupported_raw.has_value());
    if (run.value.unsupported_address) CHECK(*run.value.unsupported_address == jojo::kDreamcastBootLoadAddress + 2u);
    if (run.value.unsupported_raw) CHECK(*run.value.unsupported_raw == 0xFFFFu);
    CHECK(run.value.state.pc == jojo::kDreamcastBootLoadAddress);
}

static void test_preserves_initial_cpu_state_and_ram_side_effects() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u);
    jojo::Sh4ReferenceState initial{};
    initial.r[1] = jojo::kDreamcastMainRamCachedBase + 0x200u;
    initial.r[2] = 0xA1B2C3D4u;
    initial.gbr = 0x12345678u;
    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(run.value.state.gbr == 0x12345678u);
    const auto stored = jojo::read_dreamcast_u32(run.value.memory,
        jojo::kDreamcastMainRamCachedBase + 0x200u);
    CHECK(stored);
    if (stored) CHECK(stored.value == 0xA1B2C3D4u);
}

static void test_boot_harness_routes_system_asic_interrupt_mask_access() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u);
    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F6910u;
    initial.r[2] = 0x00001000u;
    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.operations_executed == 1u);
}

static void test_boot_harness_routes_pvr2_spg_timing_access() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u);
    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F80CCu;
    initial.r[2] = 0x00000004u;
    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.operations_executed == 1u);
}

static void test_boot_harness_routes_maple_enable_register() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u);
    append_word(program, 0x6312u);
    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F6C14u;
    initial.r[2] = 0x00000001u;
    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.state.r[3] == 1u);
    CHECK(run.value.operations_executed == 2u);
}

static void test_boot_harness_executes_maple_device_request() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u);
    append_word(program, 0x7104u);
    append_word(program, 0x2132u);
    append_word(program, 0x7104u);
    append_word(program, 0x2142u);
    append_word(program, 0x2562u);
    append_word(program, 0x2782u);
    append_word(program, 0x2982u);
    append_word(program, 0x6BA2u);

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000400u;
    jojo::Sh4ReferenceState initial{};
    initial.r[1] = table;
    initial.r[2] = 0x80000000u;
    initial.r[3] = receive;
    initial.r[4] = 0x01200000u;
    initial.r[5] = 0xA05F6C04u;
    initial.r[6] = table;
    initial.r[7] = 0xA05F6C14u;
    initial.r[8] = 1u;
    initial.r[9] = 0xA05F6C18u;
    initial.r[10] = receive;

    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.state.r[11] == 0x0500201Cu);
    CHECK(run.value.operations_executed == 9u);
}

template <typename Input>
static void test_boot_harness_bridges_resolved_input_impl() {
    if constexpr (requires(const jojo::DreamcastBootProgram& program,
                           jojo::Sh4ReferenceState state,
                           const Input& input) {
                      jojo::run_dreamcast_boot_reference(program, state, 64u, input);
                  }) {
        jojo::DreamcastBootProgram program{};
        append_word(program, 0x2122u);
        append_word(program, 0x7104u);
        append_word(program, 0x2132u);
        append_word(program, 0x7104u);
        append_word(program, 0x2142u);
        append_word(program, 0x7104u);
        append_word(program, 0x2152u);
        append_word(program, 0x2672u);
        append_word(program, 0x2892u);
        append_word(program, 0x2A92u);
        append_word(program, 0x6CB2u);

        constexpr std::uint32_t table = 0x0C001000u;
        constexpr std::uint32_t receive = 0x0C002000u;
        jojo::Sh4ReferenceState initial{};
        initial.r[1] = table;
        initial.r[2] = 0x80000001u;
        initial.r[3] = receive;
        initial.r[4] = 0x09200001u;
        initial.r[5] = 0x00000001u;
        initial.r[6] = 0xA05F6C04u;
        initial.r[7] = table;
        initial.r[8] = 0xA05F6C14u;
        initial.r[9] = 1u;
        initial.r[10] = 0xA05F6C18u;
        initial.r[11] = receive + 8u;

        Input input{};
        input[0].actions[jojo::GameAction::attack_light] = true;
        input[0].actions[jojo::GameAction::left] = true;
        const auto run = jojo::run_dreamcast_boot_reference(program, initial, 64u, input);
        CHECK(run);
        if (!run) return;
        CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
        CHECK(!run.value.bus_fault.has_value());
        // X and Left pressed, everything else released.
        CHECK(run.value.state.r[12] == 0xFBBF0000u);
    } else {
        CHECK(false && "boot runner must accept ResolvedInputFrame for Maple polling");
    }
}

static void test_boot_harness_bridges_resolved_input() {
    test_boot_harness_bridges_resolved_input_impl<jojo::ResolvedInputFrame>();
}

static void test_boot_harness_rejects_disabled_maple_dma_start() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u);
    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F6C18u;
    initial.r[2] = 0x00000001u;
    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::unmapped_bus_access);
    CHECK(run.value.bus_fault.has_value());
    if (run.value.bus_fault) {
        CHECK(run.value.bus_fault->address == 0xA05F6C18u);
        CHECK(run.value.bus_fault->region == jojo::DreamcastBusRegion::maple);
        CHECK(run.value.bus_fault->access == jojo::DreamcastBusAccess::write);
    }
}

static void test_block_limit_is_reported_without_claiming_successful_boot() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0xAFFEu);
    append_word(program, 0x0009u);
    const auto run = jojo::run_dreamcast_boot_reference(program, {}, 3u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::block_limit);
    CHECK(run.value.blocks_executed == 3u);
}

static void test_privileged_sleep_does_not_claim_end_of_program() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x001Bu);
    jojo::Sh4ReferenceState initial{};
    initial.sr = 0x40000000u;
    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;
    CHECK(run.value.state.sleeping);
    CHECK(run.value.stop_reason != jojo::DreamcastBootStopReason::end_of_program);
    CHECK(run.value.operations_executed == 1u);
}

int main() {
    test_executes_loaded_boot_program_from_main_ram();
    test_reports_first_reachable_unsupported_opcode();
    test_preserves_initial_cpu_state_and_ram_side_effects();
    test_boot_harness_routes_system_asic_interrupt_mask_access();
    test_boot_harness_routes_pvr2_spg_timing_access();
    test_boot_harness_routes_maple_enable_register();
    test_boot_harness_executes_maple_device_request();
    test_boot_harness_bridges_resolved_input();
    test_boot_harness_rejects_disabled_maple_dma_start();
    test_block_limit_is_reported_without_claiming_successful_boot();
    test_privileged_sleep_does_not_claim_end_of_program();
    if (failures) {
        std::cerr << failures << " Dreamcast boot-runner assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast boot-runner assertions passed\n";
    return 0;
}
