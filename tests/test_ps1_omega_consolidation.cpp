#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

#include <cassert>
#include <cstdint>

int main() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};

    cpu.gpr[0] = 0xFFFFFFFFu;
    cpu.gpr[31] = 0x80012000u;
    jojo::Ps1HleBiosCall init_heap{};
    init_heap.domain = jojo::Ps1HleBiosDomain::a0;
    init_heap.selector = 0x39u;
    init_heap.a0 = 0x80040000u;
    init_heap.a1 = 0x1000u;
    init_heap.ra = cpu.gpr[31];

    const auto init_result = bios.dispatch(init_heap, cpu, bus);
    assert(init_result.disposition == jojo::Ps1HleBiosDisposition::handled);
    assert(bios.heap_state().has_value());
    assert(bios.heap_state()->base == 0x80040000u);
    assert(bios.heap_state()->size == 0x1000u);
    assert(cpu.pc == 0x80012000u);
    assert(cpu.gpr[0] == 0u);

    cpu.gpr[2] = 0x12345678u;
    cpu.gpr[31] = 0x80012020u;
    jojo::Ps1HleBiosCall remove_iso{};
    remove_iso.domain = jojo::Ps1HleBiosDomain::a0;
    remove_iso.selector = 0x72u;
    remove_iso.ra = cpu.gpr[31];
    const auto remove_result = bios.dispatch(remove_iso, cpu, bus);
    assert(remove_result.disposition == jojo::Ps1HleBiosDisposition::handled);
    assert(cpu.gpr[2] == 0x12345678u);
    assert(bios.iso9660_removed());

    const auto before_hash = bios.diagnostic_state_hash();
    const auto before_cpu = cpu;
    jojo::Ps1HleBiosCall invalid_counter{};
    invalid_counter.domain = jojo::Ps1HleBiosDomain::c0;
    invalid_counter.selector = 0x0Au;
    invalid_counter.a0 = 4u;
    invalid_counter.a1 = 1u;
    const auto invalid_result = bios.dispatch(invalid_counter, cpu, bus);
    assert(invalid_result.disposition == jojo::Ps1HleBiosDisposition::unsupported);
    assert(cpu.pc == before_cpu.pc);
    assert(cpu.next_pc == before_cpu.next_pc);
    assert(cpu.gpr[2] == before_cpu.gpr[2]);
    assert(bios.diagnostic_state_hash() == before_hash);

    jojo::Ps1HleBiosCall unknown{};
    unknown.domain = jojo::Ps1HleBiosDomain::a0;
    unknown.selector = 0xFFFFu;
    const auto unknown_result = bios.dispatch(unknown, cpu, bus);
    assert(unknown_result.disposition == jojo::Ps1HleBiosDisposition::unsupported);
    assert(bios.diagnostic_state_hash() == before_hash);

    return 0;
}
