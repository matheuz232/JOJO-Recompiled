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

static void test_boot_harness_routes_maple_enable_register() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u); // MOV.L R2,@R1
    append_word(program, 0x6312u); // MOV.L @R1,R3

    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F6C14u; // cached alias of SB_MDEN
    initial.r[2] = 0x00000001u;

    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.state.r[3] == 1u);
    CHECK(run.value.operations_executed == 2u);
}

static void test_boot_harness_executes_single_entry_maple_dma() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u); // MOV.L R2,@R1: final descriptor control
    append_word(program, 0x7104u); // ADD #4,R1
    append_word(program, 0x2132u); // MOV.L R3,@R1: receive address
    append_word(program, 0x7104u); // ADD #4,R1
    append_word(program, 0x2142u); // MOV.L R4,@R1: Maple frame header
    append_word(program, 0x2562u); // MOV.L R6,@R5: SB_MDSTAR
    append_word(program, 0x2782u); // MOV.L R8,@R7: SB_MDEN=1
    append_word(program, 0x2982u); // MOV.L R8,@R9: SB_MDST=1
    append_word(program, 0x6BA2u); // MOV.L @R10,R11: read DMA response

    constexpr std::uint32_t table = 0x0C000100u;
    constexpr std::uint32_t receive = 0x0C000200u;

    jojo::Sh4ReferenceState initial{};
    initial.r[1] = table;
    initial.r[2] = 0x80000000u; // final entry, port 0, zero payload words
    initial.r[3] = receive;
    initial.r[4] = 0x01200000u; // Device Request: destination 0x20, source 0x00
    initial.r[5] = 0xA05F6C04u; // SB_MDSTAR
    initial.r[6] = table;
    initial.r[7] = 0xA05F6C14u; // SB_MDEN
    initial.r[8] = 1u;
    initial.r[9] = 0xA05F6C18u; // SB_MDST
    initial.r[10] = receive;

    const auto run = jojo::run_dreamcast_boot_reference(program, initial, 32u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::end_of_program);
    CHECK(!run.value.bus_fault.has_value());
    CHECK(run.value.state.r[11] == 0xFFFFFFFFu);
    CHECK(run.value.operations_executed == 9u);
}

static void test_boot_harness_rejects_disabled_maple_dma_start() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x2122u); // MOV.L R2,@R1

    jojo::Sh4ReferenceState initial{};
    initial.r[1] = 0xA05F6C18u; // cached alias of SB_MDST
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
    append_word(program, 0xAFFEu); // BRA -4 -> branches to itself
    append_word(program, 0x0009u); // delay-slot NOP

    const auto run = jojo::run_dreamcast_boot_reference(program, {}, 3u);
    CHECK(run);
    if (!run) return;

    CHECK(run.value.stop_reason == jojo::DreamcastBootStopReason::block_limit);
    CHECK(run.value.blocks_executed == 3u);
}

static void test_privileged_sleep_does_not_claim_end_of_program() {
    jojo::DreamcastBootProgram program{};
    append_word(program, 0x001Bu); // SLEEP

    jojo::Sh4ReferenceState initial{};
    initial.sr = 0x40000000u; // MD=1: privileged mode

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
    test_boot_harness_executes_single_entry_maple_dma();
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
