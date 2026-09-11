#pragma once

#include "core/ps1_memory_bus.h"
#include "core/r3000a_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jojo {

struct Ps1OmegaCpuSnapshot {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t pc{};
    std::uint32_t next_pc{};
    R3000aDelayedLoad pending_load{};
    R3000aDelaySlot delay_slot{};
    R3000aCop0 cop0{};
    std::array<std::uint32_t, 32> cop2_control{};
    std::uint8_t external_interrupt_pending{};
};

struct Ps1OmegaSnapshot {
    std::uint32_t schema_version{1u};
    Ps1OmegaCpuSnapshot cpu{};

    std::uint64_t bus_state_hash{};
    std::uint64_t main_ram_hash{};
    std::uint64_t scratchpad_hash{};
    std::uint32_t common_delay{};
    std::uint16_t interrupt_status{};
    std::uint16_t interrupt_mask{};
    std::uint32_t dma2_madr{};
    std::uint32_t dma2_bcr{};
    std::uint32_t dma2_chcr{};
    std::uint32_t dma_control{};
    std::uint32_t dma_interrupt{};
    std::uint16_t timer1_counter{};
    std::uint16_t timer1_mode{};

    std::uint8_t cdrom_index{};
    std::uint8_t cdrom_drive_status{};
    std::uint8_t cdrom_interrupt_enable{};
    std::uint8_t cdrom_interrupt_status{};
    bool cdrom_irq_line{};
    bool cdrom_irq_rising_edge_pending{};
    bool cdrom_response_pending{};
    std::uint8_t cdrom_response_value{};
    std::uint64_t cdrom_command_count{};

    std::uint32_t gpu_stat{};
    std::uint32_t gpu_gp0_read_latch{};
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t gpu_command_buffer_reset_count{};
    bool gpu_display_disabled{};
    bool gpu_irq1{};
    std::uint8_t gpu_dma_direction{};
    std::uint16_t gpu_display_vram_x{};
    std::uint16_t gpu_display_vram_y{};
    std::uint16_t gpu_horizontal_start{};
    std::uint16_t gpu_horizontal_end{};
    std::uint16_t gpu_vertical_start{};
    std::uint16_t gpu_vertical_end{};
    std::uint8_t gpu_display_mode{};

    bool diagnostic_mmio_probe_enabled{};
    bool diagnostic_mmio_read_override_present{};
    Ps1DiagnosticMmioReadOverride diagnostic_mmio_read_override{};
};

namespace omega_snapshot_detail {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

[[nodiscard]] inline std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const auto value : bytes) {
        hash ^= value;
        hash *= kFnvPrime;
    }
    return hash;
}

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void boolean(bool value) { u8(static_cast<std::uint8_t>(value ? 1u : 0u)); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8u));
    }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32u; shift += 8u) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64u; shift += 8u) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool finished() const noexcept { return ok_ && cursor_ == bytes_.size(); }

    std::uint8_t u8() noexcept {
        if (cursor_ >= bytes_.size()) {
            ok_ = false;
            return 0u;
        }
        return bytes_[cursor_++];
    }
    bool boolean() noexcept {
        const auto value = u8();
        if (value > 1u) ok_ = false;
        return value != 0u;
    }
    std::uint16_t u16() noexcept {
        const auto lo = static_cast<std::uint16_t>(u8());
        const auto hi = static_cast<std::uint16_t>(u8());
        return static_cast<std::uint16_t>(lo | (hi << 8u));
    }
    std::uint32_t u32() noexcept {
        std::uint32_t value{};
        for (unsigned shift = 0; shift < 32u; shift += 8u) {
            value |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return value;
    }
    std::uint64_t u64() noexcept {
        std::uint64_t value{};
        for (unsigned shift = 0; shift < 64u; shift += 8u) {
            value |= static_cast<std::uint64_t>(u8()) << shift;
        }
        return value;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_{};
    bool ok_{true};
};

inline void write_cpu(Writer& writer, const Ps1OmegaCpuSnapshot& cpu) {
    for (const auto value : cpu.gpr) writer.u32(value);
    writer.u32(cpu.hi);
    writer.u32(cpu.lo);
    writer.u32(cpu.pc);
    writer.u32(cpu.next_pc);
    writer.boolean(cpu.pending_load.valid);
    writer.u8(cpu.pending_load.reg);
    writer.u32(cpu.pending_load.value);
    writer.boolean(cpu.delay_slot.active);
    writer.u32(cpu.delay_slot.branch_pc);
    writer.boolean(cpu.delay_slot.taken);
    writer.u32(cpu.delay_slot.target);
    writer.u32(cpu.cop0.target_address);
    writer.u32(cpu.cop0.bad_vaddr);
    writer.u32(cpu.cop0.status);
    writer.u32(cpu.cop0.cause);
    writer.u32(cpu.cop0.epc);
    for (const auto value : cpu.cop2_control) writer.u32(value);
    writer.u8(cpu.external_interrupt_pending);
}

inline void read_cpu(Reader& reader, Ps1OmegaCpuSnapshot& cpu) noexcept {
    for (auto& value : cpu.gpr) value = reader.u32();
    cpu.hi = reader.u32();
    cpu.lo = reader.u32();
    cpu.pc = reader.u32();
    cpu.next_pc = reader.u32();
    cpu.pending_load.valid = reader.boolean();
    cpu.pending_load.reg = reader.u8();
    cpu.pending_load.value = reader.u32();
    cpu.delay_slot.active = reader.boolean();
    cpu.delay_slot.branch_pc = reader.u32();
    cpu.delay_slot.taken = reader.boolean();
    cpu.delay_slot.target = reader.u32();
    cpu.cop0.target_address = reader.u32();
    cpu.cop0.bad_vaddr = reader.u32();
    cpu.cop0.status = reader.u32();
    cpu.cop0.cause = reader.u32();
    cpu.cop0.epc = reader.u32();
    for (auto& value : cpu.cop2_control) value = reader.u32();
    cpu.external_interrupt_pending = reader.u8();
}

} // namespace omega_snapshot_detail

[[nodiscard]] inline Ps1OmegaSnapshot capture_ps1_omega_snapshot(
    const R3000aState& cpu,
    const Ps1MemoryBus& bus) noexcept {
    Ps1OmegaSnapshot snapshot{};
    snapshot.cpu.gpr = cpu.gpr;
    snapshot.cpu.hi = cpu.hi;
    snapshot.cpu.lo = cpu.lo;
    snapshot.cpu.pc = cpu.pc;
    snapshot.cpu.next_pc = cpu.next_pc;
    snapshot.cpu.pending_load = cpu.pending_load;
    snapshot.cpu.delay_slot = cpu.delay_slot;
    snapshot.cpu.cop0 = cpu.cop0;
    snapshot.cpu.cop2_control = cpu.cop2_gte.control;
    snapshot.cpu.external_interrupt_pending = cpu.external_interrupt_pending;

    snapshot.bus_state_hash = bus.diagnostic_state_hash();
    snapshot.main_ram_hash = omega_snapshot_detail::hash_bytes(bus.main_ram_bytes());
    snapshot.scratchpad_hash = omega_snapshot_detail::hash_bytes(bus.scratchpad_bytes());
    snapshot.common_delay = bus.common_delay();
    snapshot.interrupt_status = bus.interrupt_status();
    snapshot.interrupt_mask = bus.interrupt_mask();
    snapshot.dma2_madr = bus.dma2_madr();
    snapshot.dma2_bcr = bus.dma2_bcr();
    snapshot.dma2_chcr = bus.dma2_chcr();
    snapshot.dma_control = bus.dma_control();
    snapshot.dma_interrupt = bus.dma_interrupt();
    snapshot.timer1_counter = bus.timer1_counter();
    snapshot.timer1_mode = bus.timer1_mode();

    const auto& cdrom = bus.cdrom();
    snapshot.cdrom_index = cdrom.index();
    snapshot.cdrom_drive_status = cdrom.drive_status();
    snapshot.cdrom_interrupt_enable = cdrom.interrupt_enable();
    snapshot.cdrom_interrupt_status = cdrom.interrupt_status();
    snapshot.cdrom_irq_line = cdrom.irq_line();
    snapshot.cdrom_irq_rising_edge_pending = cdrom.irq_rising_edge_pending();
    snapshot.cdrom_response_pending = cdrom.response_pending();
    snapshot.cdrom_response_value = cdrom.response_value().value_or(0u);
    snapshot.cdrom_command_count = cdrom.command_count();

    const auto& gpu = bus.gpu();
    snapshot.gpu_stat = gpu.gpu_stat();
    snapshot.gpu_gp0_read_latch = gpu.gp0_read_latch();
    snapshot.gpu_gp0_command_count = gpu.gp0_command_count();
    snapshot.gpu_gp1_command_count = gpu.gp1_command_count();
    snapshot.gpu_command_buffer_reset_count = gpu.command_buffer_reset_count();
    snapshot.gpu_display_disabled = gpu.display_disabled();
    snapshot.gpu_irq1 = gpu.irq1();
    snapshot.gpu_dma_direction = gpu.dma_direction();
    snapshot.gpu_display_vram_x = gpu.display_vram_x();
    snapshot.gpu_display_vram_y = gpu.display_vram_y();
    snapshot.gpu_horizontal_start = gpu.horizontal_start();
    snapshot.gpu_horizontal_end = gpu.horizontal_end();
    snapshot.gpu_vertical_start = gpu.vertical_start();
    snapshot.gpu_vertical_end = gpu.vertical_end();
    snapshot.gpu_display_mode = gpu.display_mode();

    snapshot.diagnostic_mmio_probe_enabled = bus.diagnostic_mmio_probe_enabled();
    if (const auto& override_value = bus.diagnostic_mmio_read_override()) {
        snapshot.diagnostic_mmio_read_override_present = true;
        snapshot.diagnostic_mmio_read_override = *override_value;
    }
    return snapshot;
}

[[nodiscard]] inline std::vector<std::uint8_t> encode_ps1_omega_snapshot(
    const Ps1OmegaSnapshot& snapshot) {
    omega_snapshot_detail::Writer writer;
    writer.u32(snapshot.schema_version);
    omega_snapshot_detail::write_cpu(writer, snapshot.cpu);
    writer.u64(snapshot.bus_state_hash);
    writer.u64(snapshot.main_ram_hash);
    writer.u64(snapshot.scratchpad_hash);
    writer.u32(snapshot.common_delay);
    writer.u16(snapshot.interrupt_status);
    writer.u16(snapshot.interrupt_mask);
    writer.u32(snapshot.dma2_madr);
    writer.u32(snapshot.dma2_bcr);
    writer.u32(snapshot.dma2_chcr);
    writer.u32(snapshot.dma_control);
    writer.u32(snapshot.dma_interrupt);
    writer.u16(snapshot.timer1_counter);
    writer.u16(snapshot.timer1_mode);
    writer.u8(snapshot.cdrom_index);
    writer.u8(snapshot.cdrom_drive_status);
    writer.u8(snapshot.cdrom_interrupt_enable);
    writer.u8(snapshot.cdrom_interrupt_status);
    writer.boolean(snapshot.cdrom_irq_line);
    writer.boolean(snapshot.cdrom_irq_rising_edge_pending);
    writer.boolean(snapshot.cdrom_response_pending);
    writer.u8(snapshot.cdrom_response_value);
    writer.u64(snapshot.cdrom_command_count);
    writer.u32(snapshot.gpu_stat);
    writer.u32(snapshot.gpu_gp0_read_latch);
    writer.u64(snapshot.gpu_gp0_command_count);
    writer.u64(snapshot.gpu_gp1_command_count);
    writer.u64(snapshot.gpu_command_buffer_reset_count);
    writer.boolean(snapshot.gpu_display_disabled);
    writer.boolean(snapshot.gpu_irq1);
    writer.u8(snapshot.gpu_dma_direction);
    writer.u16(snapshot.gpu_display_vram_x);
    writer.u16(snapshot.gpu_display_vram_y);
    writer.u16(snapshot.gpu_horizontal_start);
    writer.u16(snapshot.gpu_horizontal_end);
    writer.u16(snapshot.gpu_vertical_start);
    writer.u16(snapshot.gpu_vertical_end);
    writer.u8(snapshot.gpu_display_mode);
    writer.boolean(snapshot.diagnostic_mmio_probe_enabled);
    writer.boolean(snapshot.diagnostic_mmio_read_override_present);
    writer.u32(snapshot.diagnostic_mmio_read_override.guest_address);
    writer.u32(snapshot.diagnostic_mmio_read_override.physical_address);
    writer.u8(snapshot.diagnostic_mmio_read_override.width);
    writer.u32(snapshot.diagnostic_mmio_read_override.value);
    return writer.take();
}

[[nodiscard]] inline std::optional<Ps1OmegaSnapshot> decode_ps1_omega_snapshot(
    std::span<const std::uint8_t> bytes) noexcept {
    omega_snapshot_detail::Reader reader(bytes);
    Ps1OmegaSnapshot snapshot{};
    snapshot.schema_version = reader.u32();
    if (snapshot.schema_version != 1u) return std::nullopt;
    omega_snapshot_detail::read_cpu(reader, snapshot.cpu);
    snapshot.bus_state_hash = reader.u64();
    snapshot.main_ram_hash = reader.u64();
    snapshot.scratchpad_hash = reader.u64();
    snapshot.common_delay = reader.u32();
    snapshot.interrupt_status = reader.u16();
    snapshot.interrupt_mask = reader.u16();
    snapshot.dma2_madr = reader.u32();
    snapshot.dma2_bcr = reader.u32();
    snapshot.dma2_chcr = reader.u32();
    snapshot.dma_control = reader.u32();
    snapshot.dma_interrupt = reader.u32();
    snapshot.timer1_counter = reader.u16();
    snapshot.timer1_mode = reader.u16();
    snapshot.cdrom_index = reader.u8();
    snapshot.cdrom_drive_status = reader.u8();
    snapshot.cdrom_interrupt_enable = reader.u8();
    snapshot.cdrom_interrupt_status = reader.u8();
    snapshot.cdrom_irq_line = reader.boolean();
    snapshot.cdrom_irq_rising_edge_pending = reader.boolean();
    snapshot.cdrom_response_pending = reader.boolean();
    snapshot.cdrom_response_value = reader.u8();
    snapshot.cdrom_command_count = reader.u64();
    snapshot.gpu_stat = reader.u32();
    snapshot.gpu_gp0_read_latch = reader.u32();
    snapshot.gpu_gp0_command_count = reader.u64();
    snapshot.gpu_gp1_command_count = reader.u64();
    snapshot.gpu_command_buffer_reset_count = reader.u64();
    snapshot.gpu_display_disabled = reader.boolean();
    snapshot.gpu_irq1 = reader.boolean();
    snapshot.gpu_dma_direction = reader.u8();
    snapshot.gpu_display_vram_x = reader.u16();
    snapshot.gpu_display_vram_y = reader.u16();
    snapshot.gpu_horizontal_start = reader.u16();
    snapshot.gpu_horizontal_end = reader.u16();
    snapshot.gpu_vertical_start = reader.u16();
    snapshot.gpu_vertical_end = reader.u16();
    snapshot.gpu_display_mode = reader.u8();
    snapshot.diagnostic_mmio_probe_enabled = reader.boolean();
    snapshot.diagnostic_mmio_read_override_present = reader.boolean();
    snapshot.diagnostic_mmio_read_override.guest_address = reader.u32();
    snapshot.diagnostic_mmio_read_override.physical_address = reader.u32();
    snapshot.diagnostic_mmio_read_override.width = reader.u8();
    snapshot.diagnostic_mmio_read_override.value = reader.u32();
    if (!reader.finished()) return std::nullopt;
    return snapshot;
}

} // namespace jojo
