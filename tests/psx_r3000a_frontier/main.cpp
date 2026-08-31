#include "core/psx_diagnostics.h"
#include "core/psx_r3000a.h"
#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>
#include <string_view>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) | imm;
}

static std::uint32_t encode_r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                              std::uint8_t shamt, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(shamt) << 6u) | funct;
}

static std::uint32_t encode_j(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) | (target & 0x03ffffffu);
}

static void test_commercial_integer_frontier() {
    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c674u);
    cpu.gpr[5] = 0x33330000u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_i(0x0d, 5, 5, 0x3333u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[5] == 0x33333333u);
    CHECK(cpu.pc == 0x8003c678u);
    CHECK(cpu.next_pc == 0x8003c67cu);

    jojo::reset_psx_r3000a(cpu, 0x80001000u);
    cpu.gpr[2] = 0x12340000u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_i(0x0d, 2, 3, 0xffffu)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[3] == 0x1234ffffu);

    jojo::reset_psx_r3000a(cpu, 0x8003c948u);
    cpu.gpr[3] = 0x89abcdefu;
    CHECK(jojo::step_psx_r3000a(cpu, encode_i(0x0c, 3, 19, 0xffffu)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[19] == 0x0000cdefu);
    CHECK(cpu.pc == 0x8003c94cu);
    CHECK(cpu.next_pc == 0x8003c950u);

    jojo::reset_psx_r3000a(cpu, 0x80001000u);
    cpu.gpr[2] = 0xffff0000u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_i(0x0c, 2, 3, 0x8001u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[3] == 0u);

    jojo::reset_psx_r3000a(cpu, 0x8003c950u);
    cpu.gpr[17] = 4u;
    cpu.gpr[3] = 1u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_r(17, 3, 3, 0, 0x04)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[3] == 0x10u);
    CHECK(cpu.pc == 0x8003c954u);
    CHECK(cpu.next_pc == 0x8003c958u);

    jojo::reset_psx_r3000a(cpu, 0x80001000u);
    cpu.gpr[2] = 0x21u;
    cpu.gpr[3] = 3u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_r(2, 3, 4, 0, 0x04)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[4] == 6u);

    jojo::reset_psx_r3000a(cpu, 0x8003c964u);
    cpu.gpr[31] = 0x12345678u;
    CHECK(jojo::step_psx_r3000a(cpu, encode_j(0x02u, 0x0000f263u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[31] == 0x12345678u);
    CHECK(cpu.pc == 0x8003c968u);
    CHECK(cpu.next_pc == 0x8003c98cu);
    CHECK(jojo::step_psx_r3000a(cpu, 0u).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003c98cu);

    jojo::reset_psx_r3000a(cpu, 0x9ffffffcu);
    CHECK(jojo::step_psx_r3000a(cpu, encode_j(0x02u, 1u)).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.next_pc == 0xa0000004u);
}

static void test_gpu_gp0_and_gpu_cw() {
    jojo::PsxBus bus{};
    CHECK(jojo::psx_bus_write_u32(bus, 0x1f801810u, 0xe1000400u) == jojo::PsxBusAccessReason::ok);

    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000a0u);
    runtime.cpu.gpr[4] = 0xe1000400u;
    runtime.cpu.gpr[9] = 0x49u;
    runtime.cpu.gpr[31] = 0x8003ca10u;
    runtime.cpu.gpr[2] = 0xffffffffu;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.cpu.pc == 0x8003ca10u);
    CHECK(runtime.cpu.next_pc == 0x8003ca14u);
}

static void test_change_clear_pad() {
    jojo::PsxRuntime runtime{};
    runtime.bios.pad_card_irq_completes = true;
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 0u;
    runtime.cpu.gpr[9] = 0x5bu;
    runtime.cpu.gpr[31] = 0x8003c9a0u;
    runtime.cpu.gpr[2] = 0x13579bdfu;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(!runtime.bios.pad_card_irq_completes);
    CHECK(runtime.cpu.gpr[2] == 0x13579bdfu);
    CHECK(runtime.cpu.pc == 0x8003c9a0u);
    CHECK(runtime.cpu.next_pc == 0x8003c9a4u);

    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 1u;
    runtime.cpu.gpr[9] = 0x5bu;
    runtime.cpu.gpr[31] = 0x8003c9b0u;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.bios.pad_card_irq_completes);
    CHECK(runtime.cpu.pc == 0x8003c9b0u);
}

static void test_change_clear_rcnt() {
    jojo::PsxRuntime runtime{};
    runtime.bios.timer_vblank_irq_auto_ack[3] = true;
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000c0u);
    runtime.cpu.gpr[4] = 3u;
    runtime.cpu.gpr[5] = 0u;
    runtime.cpu.gpr[9] = 0x0au;
    runtime.cpu.gpr[31] = 0x8003c9acu;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 1u);
    CHECK(!runtime.bios.timer_vblank_irq_auto_ack[3]);
    CHECK(runtime.cpu.pc == 0x8003c9acu);
    CHECK(runtime.cpu.next_pc == 0x8003c9b0u);

    jojo::reset_psx_r3000a(runtime.cpu, 0x000000c0u);
    runtime.cpu.gpr[4] = 3u;
    runtime.cpu.gpr[5] = 1u;
    runtime.cpu.gpr[9] = 0x0au;
    runtime.cpu.gpr[31] = 0x8003c9bcu;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.bios.timer_vblank_irq_auto_ack[3]);
    CHECK(runtime.cpu.pc == 0x8003c9bcu);
}

static void test_broken_96_remove_leaves_cdrom_irq_chain_installed() {
    jojo::PsxRuntime runtime{};
    runtime.bios.cdrom_irq_handlers_installed = true;
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000a0u);
    runtime.cpu.gpr[4] = 0x80062720u;
    runtime.cpu.gpr[9] = 0x72u;
    runtime.cpu.gpr[31] = 0x8003c6f0u;
    runtime.cpu.gpr[2] = 0x2468ace0u;

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.bios.cdrom_irq_handlers_installed);
    CHECK(runtime.cpu.gpr[2] == 0x2468ace0u);
    CHECK(runtime.cpu.pc == 0x8003c6f0u);
    CHECK(runtime.cpu.next_pc == 0x8003c6f4u);
}

static void test_critical_section_syscalls_return_through_exception_state() {
    jojo::PsxRuntime runtime{};

    CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x80035a70u, 0x0000000cu) == jojo::PsxBusAccessReason::ok);
    jojo::reset_psx_r3000a(runtime.cpu, 0x80035a70u);
    runtime.cpu.cop0.status = 0x10000401u;
    runtime.cpu.gpr[4] = 1u;
    runtime.cpu.gpr[2] = 0xdeadbeefu;
    const auto enter = jojo::step_psx_runtime(runtime);
    CHECK(enter.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 1u);
    CHECK(runtime.cpu.cop0.epc == 0x80035a70u);
    CHECK((runtime.cpu.cop0.cause & 0x7cu) == 0x20u);
    CHECK(runtime.cpu.cop0.status == 0x10000000u);
    CHECK(runtime.cpu.pc == 0x80035a74u);
    CHECK(runtime.cpu.next_pc == 0x80035a78u);

    CHECK(jojo::psx_bus_write_u32(runtime.bus, 0x80035a84u, 0x0000000cu) == jojo::PsxBusAccessReason::ok);
    jojo::reset_psx_r3000a(runtime.cpu, 0x80035a84u);
    runtime.cpu.cop0.status = 0x10000000u;
    runtime.cpu.gpr[4] = 2u;
    runtime.cpu.gpr[2] = 0x2468ace0u;
    const auto exit = jojo::step_psx_runtime(runtime);
    CHECK(exit.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0x2468ace0u);
    CHECK(runtime.cpu.cop0.epc == 0x80035a84u);
    CHECK((runtime.cpu.cop0.cause & 0x7cu) == 0x20u);
    CHECK(runtime.cpu.cop0.status == 0x10000401u);
    CHECK(runtime.cpu.pc == 0x80035a88u);
    CHECK(runtime.cpu.next_pc == 0x80035a8cu);

    jojo::reset_psx_r3000a(runtime.cpu, 0x80035a70u);
    runtime.cpu.cop0.status = 0x00000400u;
    runtime.cpu.gpr[4] = 1u;
    runtime.cpu.gpr[2] = 0xffffffffu;
    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0u);
    CHECK(runtime.cpu.cop0.status == 0u);
}

static void test_reset_entry_int_restores_scph1001_default_exit_structure() {
    jojo::PsxRuntime runtime{};
    runtime.bios.entry_interrupt_hook_installed = true;
    runtime.bios.entry_interrupt_hook_address = 0x800616bcu;
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[9] = 0x18u;
    runtime.cpu.gpr[31] = 0x8003caacu;
    runtime.cpu.gpr[2] = 0xffffffffu;

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(!runtime.bios.entry_interrupt_hook_installed);
    CHECK(runtime.bios.entry_interrupt_hook_address == 0x00006cf4u);
    CHECK(runtime.cpu.gpr[2] == 0x00006cf4u);
    CHECK(runtime.cpu.pc == 0x8003caacu);
    CHECK(runtime.cpu.next_pc == 0x8003cab0u);

    const auto default_pc = jojo::psx_bus_read_u32(runtime.bus, 0x00006cf4u);
    const auto default_sp = jojo::psx_bus_read_u32(runtime.bus, 0x00006cf8u);
    CHECK(default_pc.reason == jojo::PsxBusAccessReason::ok);
    CHECK(default_pc.value == 0x00000f40u);
    CHECK(default_sp.reason == jojo::PsxBusAccessReason::ok);
    CHECK(default_sp.value == 0x000085d4u);
    for (std::uint32_t address = 0x00006cfcu; address <= 0x00006d20u; address += 4u) {
        const auto saved = jojo::psx_bus_read_u32(runtime.bus, address);
        CHECK(saved.reason == jojo::PsxBusAccessReason::ok);
        CHECK(saved.value == 0u);
    }
}

static void test_bios_dummy_stdout_write_returns_real_length() {
    jojo::PsxRuntime runtime{};
    constexpr std::uint32_t source = 0x800973a8u;
    constexpr std::uint32_t length = 0x20u;
    for (std::uint32_t i = 0; i < length; ++i) {
        CHECK(jojo::psx_bus_write_u8(runtime.bus, source + i,
                                     static_cast<std::uint8_t>(0x40u + i)) ==
              jojo::PsxBusAccessReason::ok);
    }

    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 1u;
    runtime.cpu.gpr[5] = source;
    runtime.cpu.gpr[6] = length;
    runtime.cpu.gpr[7] = 0xffffffffu;
    runtime.cpu.gpr[9] = 0x35u;
    runtime.cpu.gpr[31] = 0x80039504u;
    runtime.cpu.gpr[2] = 0xffffffffu;

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == length);
    CHECK(runtime.cpu.pc == 0x80039504u);
    CHECK(runtime.cpu.next_pc == 0x80039508u);

    jojo::reset_psx_r3000a(runtime.cpu, 0x000000b0u);
    runtime.cpu.gpr[4] = 2u;
    runtime.cpu.gpr[5] = source;
    runtime.cpu.gpr[6] = length;
    runtime.cpu.gpr[9] = 0x35u;
    CHECK(jojo::step_psx_runtime(runtime).reason ==
          jojo::PsxR3000aStepReason::unsupported_instruction);
}

static void test_exception_diagnostic_names_are_stable() {
    CHECK(std::string_view(jojo::psx_r3000a_exception_code_name(jojo::PsxR3000aExceptionCode::interrupt)) == "interrupt");
    CHECK(std::string_view(jojo::psx_r3000a_exception_code_name(jojo::PsxR3000aExceptionCode::address_error_load)) == "address_error_load");
    CHECK(std::string_view(jojo::psx_r3000a_exception_code_name(jojo::PsxR3000aExceptionCode::address_error_store)) == "address_error_store");
    CHECK(std::string_view(jojo::psx_r3000a_exception_code_name(jojo::PsxR3000aExceptionCode::reserved_instruction)) == "reserved_instruction");
    CHECK(std::string_view(jojo::psx_r3000a_exception_code_name(jojo::PsxR3000aExceptionCode::overflow)) == "overflow");
    CHECK(std::string_view(jojo::psx_r3000a_exception_code_name(jojo::PsxR3000aExceptionCode::none)) == "none");
}

int main() {
    test_commercial_integer_frontier();
    test_gpu_gp0_and_gpu_cw();
    test_change_clear_pad();
    test_change_clear_rcnt();
    test_broken_96_remove_leaves_cdrom_irq_chain_installed();
    test_critical_section_syscalls_return_through_exception_state();
    test_reset_entry_int_restores_scph1001_default_exit_structure();
    test_bios_dummy_stdout_write_returns_real_length();
    test_exception_diagnostic_names_are_stable();
    if (failures) return 1;
    std::cout << "R3000A commercial frontier assertions passed\n";
    return 0;
}