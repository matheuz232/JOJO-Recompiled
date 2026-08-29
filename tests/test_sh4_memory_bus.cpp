#include "core/dreamcast_bus.h"
#include "core/sh4_cfg.h"
#include "core/sh4_ir.h"
#include "core/sh4_reference_executor.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8u));
}

static jojo::Sh4IrProgram make_program(const std::vector<std::uint16_t>& words) {
    std::vector<std::uint8_t> bytes;
    for (const auto word : words) append_word(bytes, word);
    const auto cfg = jojo::build_sh4_cfg(bytes, 0x8C010000u, 0x8C010000u);
    CHECK(cfg);
    if (!cfg) return {};
    const auto ir = jojo::lift_sh4_cfg(cfg.value);
    CHECK(ir);
    return ir ? ir.value : jojo::Sh4IrProgram{};
}

static jojo::DreamcastExecutableMemory blank_memory() {
    jojo::DreamcastBootProgram program{};
    program.bytes = {0x09u, 0x00u}; // NOP, only to allocate canonical RAM backing.
    const auto loaded = jojo::load_dreamcast_boot_memory(program);
    CHECK(loaded);
    return loaded ? loaded.value : jojo::DreamcastExecutableMemory{};
}

static void test_executor_uses_dreamcast_ram_aliases_through_bus() {
    auto memory = blank_memory();
    jojo::DreamcastReferenceBus bus(memory);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0xAC000200u; // uncached alias
    state.r[2] = 0xA1B2C3D4u;

    const auto ir = make_program({
        0x2122u, // MOV.L R2,@R1
    });
    const auto run = jojo::execute_sh4_ir_reference(ir, state, bus, 8u);
    CHECK(run);
    if (!run) return;

    const auto cached = jojo::read_dreamcast_u32(memory, 0x8C000200u);
    const auto physical = jojo::read_dreamcast_u32(memory, 0x0C000200u);
    CHECK(cached && physical);
    if (cached) CHECK(cached.value == 0xA1B2C3D4u);
    if (physical) CHECK(physical.value == 0xA1B2C3D4u);
}

static void test_executor_can_load_through_a_different_alias() {
    auto memory = blank_memory();
    const auto stored = jojo::write_dreamcast_u32(memory, 0x0C000300u, 0x78563412u);
    CHECK(stored);
    jojo::DreamcastReferenceBus bus(memory);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x8C000300u; // cached alias

    const auto ir = make_program({
        0x6212u, // MOV.L @R1,R2
    });
    const auto run = jojo::execute_sh4_ir_reference(ir, state, bus, 8u);
    CHECK(run);
    if (run) CHECK(state.r[2] == 0x78563412u);
}

static void test_unmapped_bus_access_fails_explicitly() {
    auto memory = blank_memory();
    jojo::DreamcastReferenceBus bus(memory);
    jojo::Sh4ReferenceState state{};
    state.r[1] = 0x04000000u; // not main RAM; no MMIO handler yet
    state.r[2] = 0x11223344u;

    const auto ir = make_program({
        0x2122u, // MOV.L R2,@R1
    });
    const auto run = jojo::execute_sh4_ir_reference(ir, state, bus, 8u);
    CHECK(!run);
    if (!run) CHECK(run.error == jojo::ErrorCode::invalid_argument);
}

int main() {
    test_executor_uses_dreamcast_ram_aliases_through_bus();
    test_executor_can_load_through_a_different_alias();
    test_unmapped_bus_access_fails_explicitly();
    if (failures) {
        std::cerr << failures << " SH-4 memory-bus assertion(s) failed\n";
        return 1;
    }
    std::cout << "all SH-4 memory-bus assertions passed\n";
    return 0;
}
