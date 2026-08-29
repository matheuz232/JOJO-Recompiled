#include "core/dreamcast_interrupts.h"
#include "core/dreamcast_system_asic.h"
#include "core/sh4_reference_executor.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void write32(jojo::DreamcastSystemAsic& asic, std::uint32_t address, std::uint32_t value) {
    CHECK(asic.write8(address + 0u, static_cast<std::uint8_t>(value)));
    CHECK(asic.write8(address + 1u, static_cast<std::uint8_t>(value >> 8u)));
    CHECK(asic.write8(address + 2u, static_cast<std::uint8_t>(value >> 16u)));
    CHECK(asic.write8(address + 3u, static_cast<std::uint8_t>(value >> 24u)));
}

static void test_pending_holly_irq_enters_sh4_interrupt_vector() {
    jojo::DreamcastSystemAsic asic;
    write32(asic, 0x005F6910u, 0x00000004u); // IRQ13 normal mask
    asic.raise_normal(0x00000004u);

    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010100u;
    state.vbr = 0x8C000000u;
    state.r[15] = 0x8CFF0000u;

    const auto delivered = jojo::service_dreamcast_system_irq(asic, state);
    CHECK(delivered);
    if (!delivered) return;
    CHECK(delivered.value);
    CHECK(state.spc == 0x8C010100u);
    CHECK(state.pc == 0x8C000600u);
    CHECK(state.intevt == 0x00000240u);
}

static void test_cpu_mask_keeps_pending_holly_irq_undelivered() {
    jojo::DreamcastSystemAsic asic;
    write32(asic, 0x005F6910u, 0x00000004u);
    asic.raise_normal(0x00000004u);

    jojo::Sh4ReferenceState state{};
    state.pc = 0x8C010100u;
    state.sr = 13u << 4u;

    const auto delivered = jojo::service_dreamcast_system_irq(asic, state);
    CHECK(delivered);
    if (delivered) CHECK(!delivered.value);
    CHECK(state.pc == 0x8C010100u);
    CHECK(asic.pending_irq_level().has_value());
}

int main() {
    test_pending_holly_irq_enters_sh4_interrupt_vector();
    test_cpu_mask_keeps_pending_holly_irq_undelivered();
    if (failures) {
        std::cerr << failures << " Dreamcast IRQ delivery assertion(s) failed\n";
        return 1;
    }
    std::cout << "all Dreamcast IRQ delivery assertions passed\n";
    return 0;
}
