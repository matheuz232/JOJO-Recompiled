#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"
#include "core/ps1_omega_snapshot.h"

#include <cassert>
#include <cstdint>

static void test_omega_snapshot_round_trip() {
    jojo::R3000aState cpu{};
    for (std::size_t i = 0; i < cpu.gpr.size(); ++i) {
        cpu.gpr[i] = 0x10000000u + static_cast<std::uint32_t>(i * 0x101u);
    }
    cpu.hi = 0x11223344u;
    cpu.lo = 0x55667788u;
    cpu.pc = 0x8004B4A4u;
    cpu.next_pc = 0x8004B4A8u;
    cpu.pending_load = {true, 7u, 0xCAFEBABEu};
    cpu.delay_slot = {true, 0x8004B4A0u, true, 0x80050000u};
    cpu.cop0.target_address = 0xDEADBEEFu;
    cpu.cop0.bad_vaddr = 0x1F801802u;
    cpu.cop0.status = 0x10900000u;
    cpu.cop0.cause = 0x00000400u;
    cpu.cop0.epc = 0x8004B4A4u;
    for (std::size_t i = 0; i < cpu.cop2_gte.control.size(); ++i) {
        cpu.cop2_gte.control[i] = 0x20000000u + static_cast<std::uint32_t>(i);
    }
    cpu.external_interrupt_pending = 1u;

    jojo::Ps1MemoryBus bus;
    assert(bus.write32(0x1F8010A0u, 0x00123450u).status == jojo::R3000aBusStatus::ok);
    assert(bus.write32(0x1F8010A4u, 0x00010020u).status == jojo::R3000aBusStatus::ok);
    assert(bus.write32(0x1F8010F0u, 0x33333333u).status == jojo::R3000aBusStatus::ok);
    assert(bus.write32(0x1F8010F4u, 0x00FF807Fu).status == jojo::R3000aBusStatus::ok);
    assert(bus.write32(0x1F801114u, 0x00000100u).status == jojo::R3000aBusStatus::ok);
    bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    assert(bus.gpu().write_gp1(0x03000000u));

    const auto snapshot = jojo::capture_ps1_omega_snapshot(cpu, bus);
    assert(snapshot.schema_version == 1u);
    assert(snapshot.cpu.gpr == cpu.gpr);
    assert(snapshot.cpu.hi == cpu.hi);
    assert(snapshot.cpu.lo == cpu.lo);
    assert(snapshot.cpu.pc == cpu.pc);
    assert(snapshot.cpu.next_pc == cpu.next_pc);
    assert(snapshot.cpu.pending_load.valid == cpu.pending_load.valid);
    assert(snapshot.cpu.pending_load.reg == cpu.pending_load.reg);
    assert(snapshot.cpu.pending_load.value == cpu.pending_load.value);
    assert(snapshot.cpu.delay_slot.active == cpu.delay_slot.active);
    assert(snapshot.cpu.delay_slot.branch_pc == cpu.delay_slot.branch_pc);
    assert(snapshot.cpu.delay_slot.taken == cpu.delay_slot.taken);
    assert(snapshot.cpu.delay_slot.target == cpu.delay_slot.target);
    assert(snapshot.cpu.cop0.status == cpu.cop0.status);
    assert(snapshot.cpu.cop0.cause == cpu.cop0.cause);
    assert(snapshot.cpu.cop2_control == cpu.cop2_gte.control);
    assert(snapshot.cpu.external_interrupt_pending == cpu.external_interrupt_pending);
    assert(snapshot.dma2_madr == 0x00123450u);
    assert(snapshot.dma2_bcr == 0x00010020u);
    assert(snapshot.dma_control == 0x33333333u);
    assert(snapshot.dma_interrupt == bus.dma_interrupt());
    assert(snapshot.timer1_mode == 0x0100u);
    assert(snapshot.cdrom_interrupt_enable == 0x1Fu);
    assert(snapshot.gpu_display_disabled == bus.gpu().display_disabled());

    const auto encoded = jojo::encode_ps1_omega_snapshot(snapshot);
    const auto decoded = jojo::decode_ps1_omega_snapshot(encoded);
    assert(decoded.has_value());
    assert(decoded->schema_version == snapshot.schema_version);
    assert(decoded->cpu.gpr == snapshot.cpu.gpr);
    assert(decoded->cpu.cop2_control == snapshot.cpu.cop2_control);
    assert(decoded->dma2_madr == snapshot.dma2_madr);
    assert(decoded->dma2_bcr == snapshot.dma2_bcr);
    assert(decoded->dma_control == snapshot.dma_control);
    assert(decoded->cdrom_interrupt_enable == snapshot.cdrom_interrupt_enable);
    assert(decoded->gpu_stat == snapshot.gpu_stat);
    assert(jojo::encode_ps1_omega_snapshot(*decoded) == encoded);
}

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

    test_omega_snapshot_round_trip();
    return 0;
}
