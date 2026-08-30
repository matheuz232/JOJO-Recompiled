#include "core/dreamcast_boot_runner.h"

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
    append_word(program, 0xE105u); // MOV #5,R1
    append_word(program, 0x7103u); // ADD #3,R1

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
    append_word(program, 0x0009u); // NOP
    append_word(program, 0xFFFFu); // unsupported

    const auto run = jojo::run_dreamcast_boot_reference(program, {}, 32u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::unsupported_opcode);
    CHECK(run.value.unsupported_address.has_value());
    CHECK(run.value.unsupported_raw.has_value());
    if (run.value.unsupported_address) {
        CHECK(*run.value.unsupported_address == jojo::kDreamcastBootLoadAddress + 2u);
    }
    if (run.value.unsupported_raw) CHECK(*run.value.unsupported_raw == 0xFFFFu);
    CHECK(run.value.state.pc == jojo::kDreamcastBootLoadAddress);
}

static void test_preserves_initial_cpu_state_and_ram_side_effects() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u); // MOV.L R2,@R1

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
    append_word(program, 0x2122u); // MOV.L R2,@R1

    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F6910u; // cached alias of SB_IML2NRM
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
    append_word(program, 0x2122u); // MOV.L R2,@R1

    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F80CCu; // cached alias of SPG_VBLANK_INT
    initial.r[2] = 0x00000004u;

    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.operations_executed == 1u);
}

static void test_block_limit_is_reported_without_claiming_successful_boot() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0xAFFEu); // BRA -4 -> branches to itself
    append_word(program, 0x0009u); // delay-slot NOP

    const auto run = jojo::run_dreamcast_boot_reference(program, {}, 3u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::block_limit);
    CHECK(run.value.blocks_executed == 3u);
}

static void test_sleep_is_reported_as_sleep_not_end_of_program() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x001Bu); // SLEEP (privileged)
    append_word(program, 0x0009u); // NOP that must not be treated as normal continuation

    jojo::Sh4ReferenceState initial{};
    initial.sr = 0x40000000u; // MD=1: privileged mode required by SH-4 SLEEP.

    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::sleep);
    CHECK(run.value.state.sleeping);
    CHECK(run.value.state.last_system_event == jojo::Sh4ReferenceSystemEvent::sleep);
    CHECK(run.value.blocks_executed == 1u);
    CHECK(run.value.operations_executed == 1u);
}

int main() {
    test_executes_loaded_boot_program_from_main_ram();
    test_reports_first_reachable_unsupported_opcode();
    test_preserves_initial_cpu_state_and_ram_side_effects();
    test_boot_harness_routes_system_asic_interrupt_mask_access();
    test_boot_harness_routes_pvr2_spg_timing_access();
    test_block_limit_is_reported_without_claiming_successful_boot();
    test_sleep_is_reported_as_sleep_not_end_of_program();
    if (failures) {
        std::cerr << failures << " Dreamcast boot-runner assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast boot-runner assertions passed\n";
    return 0;
}