#include "core/psx_runtime.h"
#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void le32(std::vector<std::uint8_t>& file, std::size_t offset, std::uint32_t value) {
    file[offset + 0u] = static_cast<std::uint8_t>(value);
    file[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    file[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    file[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

static std::vector<std::uint8_t> synthetic_exe(std::uint32_t load = 0x80010000u,
                                                std::uint32_t pc = 0x80010000u) {
    constexpr std::size_t payload_size = 0x800u;
    std::vector<std::uint8_t> file(0x800u + payload_size, 0u);
    constexpr char magic[] = "PS-X EXE";
    for (std::size_t i = 0; i < sizeof(magic) - 1u; ++i) {
        file[i] = static_cast<std::uint8_t>(magic[i]);
    }
    le32(file, 0x10u, pc);
    le32(file, 0x14u, 0x80020000u);
    le32(file, 0x18u, load);
    le32(file, 0x1cu, static_cast<std::uint32_t>(payload_size));
    le32(file, 0x30u, 0x801ff000u);
    le32(file, 0x34u, 0x100u);

    le32(file, 0x800u + 0u, encode_i(0x0f, 0, 2, 0x8006u));      // LUI r2,8006
    le32(file, 0x800u + 4u, encode_i(0x09, 2, 2, 0x36d8u));      // ADDIU r2,r2,36d8
    le32(file, 0x800u + 8u, encode_i(0x2b, 2, 3, 0xfffcu));      // SW r3,-4(r2)
    return file;
}

static jojo::PsxSystemCnf synthetic_cnf(std::uint32_t stack = 0x801fff00u) {
    jojo::PsxSystemCnf cnf{};
    cnf.boot_iso_path = "/SLUS_010.60";
    cnf.stack = stack;
    return cnf;
}

static void test_loader_copies_payload_and_initializes_boot_registers() {
    jojo::PsxRuntime runtime{};
    const auto file = synthetic_exe();
    const auto loaded = jojo::load_psx_boot_executable(runtime, file, synthetic_cnf());
    CHECK(loaded);
    if (!loaded) return;

    CHECK(runtime.cpu.pc == 0x80010000u);
    CHECK(runtime.cpu.next_pc == 0x80010004u);
    CHECK(runtime.cpu.gpr[28] == 0x80020000u);
    CHECK(runtime.cpu.gpr[29] == 0x801fff00u);
    CHECK(runtime.cpu.gpr[30] == 0x801fff00u);

    const auto first = jojo::psx_bus_read_u32(runtime.bus, 0x80010000u);
    CHECK(first.reason == jojo::PsxBusAccessReason::ok);
    CHECK(first.value == encode_i(0x0f, 0, 2, 0x8006u));
}

static void test_system_cnf_stack_overrides_exe_stack_and_zero_falls_back() {
    {
        jojo::PsxRuntime runtime{};
        const auto loaded = jojo::load_psx_boot_executable(runtime, synthetic_exe(),
                                                            synthetic_cnf(0x801ffe00u));
        CHECK(loaded);
        if (loaded) {
            CHECK(runtime.cpu.gpr[29] == 0x801ffe00u);
            CHECK(runtime.cpu.gpr[30] == 0x801ffe00u);
        }
    }
    {
        jojo::PsxRuntime runtime{};
        const auto loaded = jojo::load_psx_boot_executable(runtime, synthetic_exe(),
                                                            synthetic_cnf(0u));
        CHECK(loaded);
        if (loaded) {
            CHECK(runtime.cpu.gpr[29] == 0x801ff100u);
            CHECK(runtime.cpu.gpr[30] == 0x801ff100u);
        }
    }
}

static void test_runtime_fetches_and_executes_instructions_from_ram() {
    jojo::PsxRuntime runtime{};
    const auto loaded = jojo::load_psx_boot_executable(runtime, synthetic_exe(), synthetic_cnf());
    CHECK(loaded);
    if (!loaded) return;
    runtime.cpu.gpr[3] = 0xdeadbeefu;

    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0x80060000u);
    CHECK(runtime.cpu.pc == 0x80010004u);

    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.cpu.gpr[2] == 0x800636d8u);
    CHECK(runtime.cpu.pc == 0x80010008u);

    CHECK(jojo::step_psx_runtime(runtime).reason == jojo::PsxR3000aStepReason::ok);
    const auto stored = jojo::psx_bus_read_u32(runtime.bus, 0x800636d4u);
    CHECK(stored.reason == jojo::PsxBusAccessReason::ok);
    CHECK(stored.value == 0xdeadbeefu);
    CHECK(runtime.cpu.pc == 0x8001000cu);
}

static void test_loader_rejects_payload_outside_main_ram_window() {
    jojo::PsxRuntime runtime{};
    const auto file = synthetic_exe(0x80800000u, 0x80800000u);
    const auto loaded = jojo::load_psx_boot_executable(runtime, file, synthetic_cnf());
    CHECK(!loaded);
    if (!loaded) CHECK(loaded.error == jojo::ErrorCode::invalid_installation);
}

static void test_fetch_fault_is_explicit_and_does_not_advance_pc() {
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x1f801000u);
    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::memory_fault);
    CHECK(runtime.cpu.pc == 0x1f801000u);
    CHECK(runtime.cpu.next_pc == 0x1f801004u);
}

static void test_bios_vectors_are_boundaries_not_executable_ram() {
    constexpr std::uint32_t vectors[] = {0x000000a0u, 0x000000b0u, 0x000000c0u};
    for (const auto vector : vectors) {
        jojo::PsxRuntime runtime{};
        jojo::reset_psx_r3000a(runtime.cpu, vector);
        runtime.cpu.gpr[9] = 0x7fu;

        const auto result = jojo::step_psx_runtime(runtime);
        CHECK(result.reason == jojo::PsxR3000aStepReason::unsupported_instruction);
        CHECK(runtime.cpu.pc == vector);
        CHECK(runtime.cpu.next_pc == vector + 4u);
    }
}

static void test_bios_a0_init_heap_initializes_hle_state_and_returns_to_ra() {
    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x000000a0u);
    runtime.cpu.gpr[4] = 0x800a0000u;
    runtime.cpu.gpr[5] = 0x00040000u;
    runtime.cpu.gpr[9] = 0x39u;
    runtime.cpu.gpr[31] = 0x80012340u;

    const auto result = jojo::step_psx_runtime(runtime);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(runtime.bios.heap_initialized);
    CHECK(runtime.bios.heap_base == 0x800a0000u);
    CHECK(runtime.bios.heap_size == 0x00040000u);
    CHECK(runtime.cpu.pc == 0x80012340u);
    CHECK(runtime.cpu.next_pc == 0x80012344u);
    CHECK(runtime.cpu.gpr[31] == 0x80012340u);
}

int main() {
    test_loader_copies_payload_and_initializes_boot_registers();
    test_system_cnf_stack_overrides_exe_stack_and_zero_falls_back();
    test_runtime_fetches_and_executes_instructions_from_ram();
    test_loader_rejects_payload_outside_main_ram_window();
    test_fetch_fault_is_explicit_and_does_not_advance_pc();
    test_bios_vectors_are_boundaries_not_executable_ram();
    test_bios_a0_init_heap_initializes_hle_state_and_returns_to_ra();
    if (failures) return 1;
    std::cout << "PS-X EXE runtime loading/fetch assertions passed\n";
    return 0;
}
