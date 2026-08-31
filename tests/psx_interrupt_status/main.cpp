#include "core/psx_bus.h"
#include "core/psx_r3000a.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::uint32_t encode_i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                              std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

int main() {
    jojo::PsxBus bus{};

    // Hardware raises pending interrupt bits; software acknowledges by writing
    // zero to bits it wants cleared while ones leave pending bits unchanged.
    bus.interrupt_status = 0x0555u;
    const auto seeded = jojo::psx_bus_read_u16(bus, 0x1f801070u);
    CHECK(seeded.reason == jojo::PsxBusAccessReason::ok);
    CHECK(seeded.value == 0x0555u);

    CHECK(jojo::psx_bus_write_u16(bus, 0x1f801070u, 0x07f0u) == jojo::PsxBusAccessReason::ok);
    const auto acknowledged = jojo::psx_bus_read_u16(bus, 0x1f801070u);
    CHECK(acknowledged.reason == jojo::PsxBusAccessReason::ok);
    CHECK(acknowledged.value == static_cast<std::uint16_t>(0x0555u & 0x07f0u));

    bus.interrupt_status = 0xffffu;
    const auto masked = jojo::psx_bus_read_u16(bus, 0x1f801070u);
    CHECK(masked.reason == jojo::PsxBusAccessReason::ok);
    CHECK(masked.value == 0x07ffu);

    jojo::PsxR3000aState cpu{};
    jojo::reset_psx_r3000a(cpu, 0x8003c678u);
    cpu.gpr[3] = 0x1f801070u;
    cpu.gpr[2] = 0u;
    bus.interrupt_status = 0x07ffu;

    const auto result = jojo::step_psx_r3000a(cpu, encode_i(0x29, 3, 2, 0u), bus);
    CHECK(result.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x8003c67cu);
    CHECK(cpu.next_pc == 0x8003c680u);
    CHECK(bus.interrupt_status == 0u);

    // Exact JoJo SLUS_010.60 sequence at 0x80050960:
    //   sw  $v0,0($v1)    ; $v1=I_STAT, $v0=FFFFFFFEh
    //   lw  $v0,4($v1)    ; I_MASK
    //   ori $v0,$v0,1
    //   sw  $v0,4($v1)
    // IRQCTRL is on-die MMIO and accepts full 32-bit stores. I_STAT only
    // observes the low 11 valid bits for acknowledge semantics.
    jojo::reset_psx_r3000a(cpu, 0x80050960u);
    cpu.gpr[3] = 0x1f801070u;
    cpu.gpr[2] = 0xfffffffeu;
    bus.interrupt_status = 0x07ffu;
    bus.interrupt_mask = 0x0020u;

    const auto sw_istat = jojo::step_psx_r3000a(cpu, encode_i(0x2b, 3, 2, 0u), bus);
    CHECK(sw_istat.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x80050964u);
    CHECK(cpu.next_pc == 0x80050968u);
    CHECK(bus.interrupt_status == 0x07feu);

    const auto lw_imask = jojo::step_psx_r3000a(cpu, encode_i(0x23, 3, 2, 4u), bus);
    CHECK(lw_imask.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.pc == 0x80050968u);
    CHECK(cpu.next_pc == 0x8005096cu);
    CHECK(cpu.pending_load_valid);
    CHECK(cpu.pending_load_register == 2u);
    CHECK((cpu.pending_load_value & 0x07ffu) == 0x0020u);

    // Advance the load delay with the game's ORI, then write the enabled mask.
    const auto ori_imask = jojo::step_psx_r3000a(cpu, encode_i(0x0d, 2, 2, 1u), bus);
    CHECK(ori_imask.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(cpu.gpr[2] == 0x0021u);

    const auto sw_imask = jojo::step_psx_r3000a(cpu, encode_i(0x2b, 3, 2, 4u), bus);
    CHECK(sw_imask.reason == jojo::PsxR3000aStepReason::ok);
    CHECK(bus.interrupt_mask == 0x0021u);

    if (failures) return 1;
    std::cout << "PSX I_STAT/I_MASK MMIO assertions passed\n";
    return 0;
}
