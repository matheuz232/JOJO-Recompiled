#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

enum class PsxBusAccessReason {
    ok,
    misaligned,
    unmapped,
};

struct PsxBusReadU16Result {
    PsxBusAccessReason reason{PsxBusAccessReason::ok};
    std::uint16_t value{};
};

struct PsxBusReadU8Result {
    PsxBusAccessReason reason{PsxBusAccessReason::ok};
    std::uint8_t value{};
};

struct PsxBusReadU32Result {
    PsxBusAccessReason reason{PsxBusAccessReason::ok};
    std::uint32_t value{};
};

struct PsxBus {
    static constexpr std::size_t main_ram_size = 2u * 1024u * 1024u;
    static constexpr std::uint32_t default_ram_mirror_window = 8u * 1024u * 1024u;
    static constexpr std::uint32_t scratchpad_address = 0x1f800000u;
    static constexpr std::size_t scratchpad_size = 1024u;
    static constexpr std::uint32_t common_delay_address = 0x1f801020u;
    static constexpr std::uint32_t interrupt_status_address = 0x1f801070u;
    static constexpr std::uint32_t interrupt_mask_address = 0x1f801074u;
    static constexpr std::uint16_t cdrom_interrupt_request = 1u << 2u;
    static constexpr std::uint32_t dma2_channel_control_address = 0x1f8010a8u;
    static constexpr std::uint32_t dma_control_address = 0x1f8010f0u;
    static constexpr std::uint32_t dma_interrupt_address = 0x1f8010f4u;
    static constexpr std::uint32_t timer1_current_address = 0x1f801110u;
    static constexpr std::uint32_t timer1_mode_address = 0x1f801114u;
    static constexpr std::uint32_t cdrom_base_address = 0x1f801800u;
    static constexpr std::uint32_t cdrom_end_address = cdrom_base_address + 3u;
    static constexpr std::uint8_t cdrom_bank_mask = 0x03u;
    static constexpr std::uint8_t cdrom_interrupt_bits = 0x1fu;
    static constexpr std::uint8_t cdrom_hc05_interrupt_bits = 0x07u;
    static constexpr std::uint8_t cdrom_read_reserved_bits = 0xe0u;
    static constexpr std::uint8_t cdrom_hsts_parameter_empty = 1u << 3u;
    static constexpr std::uint8_t cdrom_hsts_parameter_write_ready = 1u << 4u;
    static constexpr std::uint8_t cdrom_hsts_result_ready = 1u << 5u;
    static constexpr std::uint8_t cdrom_clear_parameters = 1u << 6u;
    static constexpr std::uint8_t cdrom_decoder_reset = 1u << 7u;
    static constexpr std::uint8_t cdrom_sound_map_clear = 1u << 5u;
    static constexpr std::size_t cdrom_parameter_capacity = 16u;
    static constexpr std::size_t cdrom_result_capacity = 16u;
    static constexpr std::uint8_t cdrom_command_nop = 0x01u;
    static constexpr std::uint8_t cdrom_interrupt_acknowledge = 0x03u;
    static constexpr std::uint8_t cdrom_status_motor_on = 1u << 1u;
    static constexpr std::uint32_t gpu_gp0_address = 0x1f801810u;
    static constexpr std::uint32_t gpu_gp1_address = 0x1f801814u;
    static constexpr std::uint32_t gpu_status_reset = 0x14802000u;
    static constexpr std::uint32_t gpu_status_display_disabled = 1u << 23u;
    static constexpr std::uint16_t interrupt_status_valid_bits = 0x07ffu;
    static constexpr std::uint16_t interrupt_mask_valid_bits = 0x07ffu;
    static constexpr std::uint16_t timer_mode_valid_bits = 0x1fffu;
    static constexpr std::uint32_t dma2_channel_control_mask = 0x71770703u;
    static constexpr std::uint32_t dma_channel_start_busy = 1u << 24u;
    static constexpr std::uint32_t dma_interrupt_control_mask = 0x00ff807fu;
    static constexpr std::uint32_t dma_interrupt_flag_mask = 0x7f000000u;
    static constexpr std::uint32_t dma_interrupt_master_enable = 0x00800000u;
    static constexpr std::uint32_t dma_interrupt_bus_error = 0x00008000u;
    static constexpr std::uint32_t dma_interrupt_master_flag = 0x80000000u;

    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(main_ram_size, 0u);
    std::array<std::uint8_t, scratchpad_size> scratchpad{};
    std::uint16_t common_delay{};
    std::uint16_t interrupt_status{};
    std::uint16_t interrupt_mask{};
    std::uint32_t dma2_channel_control{};
    std::uint32_t dma_control{};
    std::uint32_t dma_interrupt{};
    std::uint16_t timer1_current{};
    std::uint16_t timer1_mode{};
    std::uint8_t cdrom_index{};
    std::uint8_t cdrom_interrupt_enable{};
    std::uint8_t cdrom_interrupt_flags{};
    std::uint8_t cdrom_host_control{};
    std::uint8_t cdrom_parameter_count{};
    std::array<std::uint8_t, cdrom_result_capacity> cdrom_result_fifo{};
    std::uint8_t cdrom_result_read_index{};
    std::uint8_t cdrom_result_count{};
    // The product boot path always has the mounted game disc present. On a
    // retail drive that reset state is spindle on with the shell closed.
    std::uint8_t cdrom_status{cdrom_status_motor_on};
    std::uint32_t gpu_gp0_write_latch{};
    std::uint32_t gpu_read_latch{};
    std::uint32_t gpu_status{gpu_status_reset};
};

[[nodiscard]] inline std::uint32_t psx_bus_dma_interrupt_value(const PsxBus& bus) noexcept {
    auto value = bus.dma_interrupt &
                 (PsxBus::dma_interrupt_control_mask | PsxBus::dma_interrupt_flag_mask);
    const bool master =
        (value & PsxBus::dma_interrupt_bus_error) != 0u ||
        ((value & PsxBus::dma_interrupt_master_enable) != 0u &&
         (value & PsxBus::dma_interrupt_flag_mask) != 0u);
    if (master) value |= PsxBus::dma_interrupt_master_flag;
    return value;
}

[[nodiscard]] inline bool psx_bus_cdrom_irq_active(const PsxBus& bus) noexcept {
    return (bus.cdrom_interrupt_enable & bus.cdrom_interrupt_flags &
            PsxBus::cdrom_interrupt_bits) != 0u;
}

inline void psx_bus_latch_cdrom_irq_rising_edge(PsxBus& bus,
                                                 bool was_active) noexcept {
    if (!was_active && psx_bus_cdrom_irq_active(bus)) {
        bus.interrupt_status = static_cast<std::uint16_t>(
            bus.interrupt_status | PsxBus::cdrom_interrupt_request);
    }
}

[[nodiscard]] inline bool psx_bus_cdrom_push_result(PsxBus& bus,
                                                     std::uint8_t value) noexcept {
    if (bus.cdrom_result_count >= PsxBus::cdrom_result_capacity) return false;
    const auto index = static_cast<std::size_t>(
        (static_cast<unsigned>(bus.cdrom_result_read_index) +
         static_cast<unsigned>(bus.cdrom_result_count)) % PsxBus::cdrom_result_capacity);
    bus.cdrom_result_fifo[index] = value;
    ++bus.cdrom_result_count;
    return true;
}

[[nodiscard]] inline bool psx_bus_virtual_to_physical(std::uint32_t address,
                                                       std::uint32_t& physical) noexcept {
    if (address < 0x20000000u) {
        physical = address;
        return true;
    }
    if (address >= 0x80000000u && address < 0xa0000000u) {
        physical = address - 0x80000000u;
        return true;
    }
    if (address >= 0xa0000000u && address < 0xc0000000u) {
        physical = address - 0xa0000000u;
        return true;
    }
    return false;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_ram_offset_u16(std::uint32_t address,
                                                                std::size_t& offset) noexcept {
    if ((address & 1u) != 0u) return PsxBusAccessReason::misaligned;

    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) {
        return PsxBusAccessReason::unmapped;
    }
    if (physical >= PsxBus::default_ram_mirror_window) {
        return PsxBusAccessReason::unmapped;
    }

    offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_ram_offset(std::uint32_t address,
                                                            std::size_t& offset) noexcept {
    if ((address & 3u) != 0u) return PsxBusAccessReason::misaligned;

    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) {
        return PsxBusAccessReason::unmapped;
    }
    if (physical >= PsxBus::default_ram_mirror_window) {
        return PsxBusAccessReason::unmapped;
    }

    offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_scratchpad_offset(
    std::uint32_t address, std::size_t width, std::size_t& offset) noexcept {
    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical) ||
        physical < PsxBus::scratchpad_address) {
        return PsxBusAccessReason::unmapped;
    }

    const auto relative = physical - PsxBus::scratchpad_address;
    if (relative >= PsxBus::scratchpad_size ||
        width > PsxBus::scratchpad_size - static_cast<std::size_t>(relative)) {
        return PsxBusAccessReason::unmapped;
    }
    offset = static_cast<std::size_t>(relative);
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline std::uint8_t psx_bus_cdrom_hsts(const PsxBus& bus) noexcept {
    auto value = static_cast<std::uint8_t>(bus.cdrom_index & PsxBus::cdrom_bank_mask);
    if (bus.cdrom_parameter_count == 0u) {
        value = static_cast<std::uint8_t>(value | PsxBus::cdrom_hsts_parameter_empty);
    }
    if (bus.cdrom_parameter_count < PsxBus::cdrom_parameter_capacity) {
        value = static_cast<std::uint8_t>(value | PsxBus::cdrom_hsts_parameter_write_ready);
    }
    if (bus.cdrom_result_count != 0u) {
        value = static_cast<std::uint8_t>(value | PsxBus::cdrom_hsts_result_ready);
    }
    return value;
}

[[nodiscard]] inline PsxBusReadU8Result psx_bus_read_u8(const PsxBus& bus,
                                                         std::uint32_t address) noexcept {
    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) {
        return {PsxBusAccessReason::unmapped, 0u};
    }
    if (physical < PsxBus::default_ram_mirror_window) {
        const auto offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
        return {PsxBusAccessReason::ok, bus.ram[offset]};
    }

    if (physical == PsxBus::cdrom_base_address) {
        return {PsxBusAccessReason::ok, psx_bus_cdrom_hsts(bus)};
    }
    if (physical == PsxBus::cdrom_base_address + 1u && bus.cdrom_result_count != 0u) {
        return {PsxBusAccessReason::ok,
                bus.cdrom_result_fifo[static_cast<std::size_t>(bus.cdrom_result_read_index)]};
    }
    if (physical == PsxBus::cdrom_base_address + 3u) {
        if ((bus.cdrom_index & 1u) != 0u) {
            return {PsxBusAccessReason::ok,
                    static_cast<std::uint8_t>(PsxBus::cdrom_read_reserved_bits |
                                              (bus.cdrom_interrupt_flags &
                                               PsxBus::cdrom_interrupt_bits))};
        }
        return {PsxBusAccessReason::ok,
                static_cast<std::uint8_t>(PsxBus::cdrom_read_reserved_bits |
                                          (bus.cdrom_interrupt_enable &
                                           PsxBus::cdrom_interrupt_bits))};
    }

    std::size_t offset = 0;
    if (psx_bus_scratchpad_offset(address, 1u, offset) == PsxBusAccessReason::ok) {
        return {PsxBusAccessReason::ok, bus.scratchpad[offset]};
    }
    return {PsxBusAccessReason::unmapped, 0u};
}

[[nodiscard]] inline PsxBusReadU8Result psx_bus_read_u8(PsxBus& bus,
                                                         std::uint32_t address) noexcept {
    std::uint32_t physical = 0;
    if (psx_bus_virtual_to_physical(address, physical) &&
        physical == PsxBus::cdrom_base_address + 1u &&
        bus.cdrom_result_count != 0u) {
        const auto value = bus.cdrom_result_fifo[
            static_cast<std::size_t>(bus.cdrom_result_read_index)];
        bus.cdrom_result_read_index = static_cast<std::uint8_t>(
            (static_cast<unsigned>(bus.cdrom_result_read_index) + 1u) %
            PsxBus::cdrom_result_capacity);
        --bus.cdrom_result_count;
        if (bus.cdrom_result_count == 0u) bus.cdrom_result_read_index = 0u;
        return {PsxBusAccessReason::ok, value};
    }
    return psx_bus_read_u8(static_cast<const PsxBus&>(bus), address);
}

[[nodiscard]] inline PsxBusReadU16Result psx_bus_read_u16(const PsxBus& bus,
                                                           std::uint32_t address) noexcept {
    if ((address & 1u) != 0u) return {PsxBusAccessReason::misaligned, 0u};
    if (address == PsxBus::interrupt_status_address) {
        return {PsxBusAccessReason::ok,
                static_cast<std::uint16_t>(bus.interrupt_status & PsxBus::interrupt_status_valid_bits)};
    }
    if (address == PsxBus::interrupt_mask_address) {
        return {PsxBusAccessReason::ok,
                static_cast<std::uint16_t>(bus.interrupt_mask & PsxBus::interrupt_mask_valid_bits)};
    }
    if (address == PsxBus::timer1_current_address) {
        return {PsxBusAccessReason::ok, bus.timer1_current};
    }
    if (address == PsxBus::timer1_mode_address) {
        return {PsxBusAccessReason::ok,
                static_cast<std::uint16_t>(bus.timer1_mode & PsxBus::timer_mode_valid_bits)};
    }

    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 2u, scratchpad_offset) ==
        PsxBusAccessReason::ok) {
        const auto value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bus.scratchpad[scratchpad_offset + 0u]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bus.scratchpad[scratchpad_offset + 1u]) << 8u));
        return {PsxBusAccessReason::ok, value};
    }

    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset_u16(address, offset);
    if (reason != PsxBusAccessReason::ok) return {reason, 0u};

    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bus.ram[offset + 0u]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bus.ram[offset + 1u]) << 8u));
    return {PsxBusAccessReason::ok, value};
}

[[nodiscard]] inline PsxBusReadU32Result psx_bus_read_u32(const PsxBus& bus,
                                                           std::uint32_t address) noexcept {
    if ((address & 3u) != 0u) return {PsxBusAccessReason::misaligned, 0u};
    if (address == PsxBus::common_delay_address) {
        return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.common_delay)};
    }
    if (address == PsxBus::interrupt_mask_address) {
        return {PsxBusAccessReason::ok,
                0xbf800000u |
                static_cast<std::uint32_t>(bus.interrupt_mask & PsxBus::interrupt_mask_valid_bits)};
    }
    if (address == PsxBus::dma2_channel_control_address) {
        return {PsxBusAccessReason::ok, bus.dma2_channel_control};
    }
    if (address == PsxBus::dma_control_address) {
        return {PsxBusAccessReason::ok, bus.dma_control};
    }
    if (address == PsxBus::dma_interrupt_address) {
        return {PsxBusAccessReason::ok, psx_bus_dma_interrupt_value(bus)};
    }
    if (address == PsxBus::timer1_current_address) {
        return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer1_current)};
    }
    if (address == PsxBus::gpu_gp0_address) {
        return {PsxBusAccessReason::ok, bus.gpu_read_latch};
    }
    if (address == PsxBus::gpu_gp1_address) {
        return {PsxBusAccessReason::ok, bus.gpu_status};
    }

    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 4u, scratchpad_offset) ==
        PsxBusAccessReason::ok) {
        const auto value = static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 0u]) |
                           (static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 1u]) << 8u) |
                           (static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 2u]) << 16u) |
                           (static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 3u]) << 24u);
        return {PsxBusAccessReason::ok, value};
    }

    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset(address, offset);
    if (reason != PsxBusAccessReason::ok) return {reason, 0u};

    const auto value = static_cast<std::uint32_t>(bus.ram[offset + 0u]) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 1u]) << 8u) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 2u]) << 16u) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 3u]) << 24u);
    return {PsxBusAccessReason::ok, value};
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_write_u8(PsxBus& bus,
                                                          std::uint32_t address,
                                                          std::uint8_t value) noexcept {
    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) {
        return PsxBusAccessReason::unmapped;
    }
    if (physical < PsxBus::default_ram_mirror_window) {
        const auto offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
        bus.ram[offset] = value;
        return PsxBusAccessReason::ok;
    }

    if (physical == PsxBus::cdrom_base_address) {
        bus.cdrom_index = static_cast<std::uint8_t>(value & PsxBus::cdrom_bank_mask);
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 1u && bus.cdrom_index == 0u) {
        // Keep the command surface narrow: only the observed Nop is implemented.
        if (value != PsxBus::cdrom_command_nop ||
            bus.cdrom_parameter_count != 0u ||
            (bus.cdrom_interrupt_flags & PsxBus::cdrom_hc05_interrupt_bits) != 0u ||
            bus.cdrom_result_count != 0u) {
            return PsxBusAccessReason::unmapped;
        }

        const bool was_active = psx_bus_cdrom_irq_active(bus);
        bus.cdrom_result_read_index = 0u;
        if (!psx_bus_cdrom_push_result(bus, bus.cdrom_status)) {
            return PsxBusAccessReason::unmapped;
        }
        bus.cdrom_interrupt_flags = static_cast<std::uint8_t>(
            (bus.cdrom_interrupt_flags &
             static_cast<std::uint8_t>(~PsxBus::cdrom_hc05_interrupt_bits)) |
            PsxBus::cdrom_interrupt_acknowledge);
        bus.cdrom_parameter_count = 0u;
        psx_bus_latch_cdrom_irq_rising_edge(bus, was_active);
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 2u && bus.cdrom_index == 1u) {
        const bool was_active = psx_bus_cdrom_irq_active(bus);
        bus.cdrom_interrupt_enable =
            static_cast<std::uint8_t>(value & PsxBus::cdrom_interrupt_bits);
        psx_bus_latch_cdrom_irq_rising_edge(bus, was_active);
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 3u && bus.cdrom_index == 1u) {
        if ((value & (PsxBus::cdrom_sound_map_clear | PsxBus::cdrom_decoder_reset)) != 0u) {
            return PsxBusAccessReason::unmapped;
        }
        bus.cdrom_interrupt_flags = static_cast<std::uint8_t>(
            bus.cdrom_interrupt_flags &
            static_cast<std::uint8_t>(~(value & PsxBus::cdrom_interrupt_bits)));
        if ((value & PsxBus::cdrom_clear_parameters) != 0u) {
            bus.cdrom_parameter_count = 0u;
        }
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 3u && bus.cdrom_index == 0u) {
        if (value != 0u) return PsxBusAccessReason::unmapped;
        bus.cdrom_host_control = 0u;
        return PsxBusAccessReason::ok;
    }

    std::size_t offset = 0;
    if (psx_bus_scratchpad_offset(address, 1u, offset) == PsxBusAccessReason::ok) {
        bus.scratchpad[offset] = value;
        return PsxBusAccessReason::ok;
    }
    return PsxBusAccessReason::unmapped;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_write_u16(PsxBus& bus,
                                                           std::uint32_t address,
                                                           std::uint16_t value) noexcept {
    if ((address & 1u) != 0u) return PsxBusAccessReason::misaligned;
    if (address == PsxBus::interrupt_status_address) {
        const auto write_bits = static_cast<std::uint16_t>(value & PsxBus::interrupt_status_valid_bits);
        bus.interrupt_status = static_cast<std::uint16_t>(
            bus.interrupt_status & write_bits & PsxBus::interrupt_status_valid_bits);
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::interrupt_mask_address) {
        bus.interrupt_mask = static_cast<std::uint16_t>(value & PsxBus::interrupt_mask_valid_bits);
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer1_current_address) {
        bus.timer1_current = value;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer1_mode_address) {
        bus.timer1_mode = static_cast<std::uint16_t>(value & PsxBus::timer_mode_valid_bits);
        bus.timer1_current = 0u;
        return PsxBusAccessReason::ok;
    }

    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 2u, scratchpad_offset) ==
        PsxBusAccessReason::ok) {
        bus.scratchpad[scratchpad_offset + 0u] = static_cast<std::uint8_t>(value);
        bus.scratchpad[scratchpad_offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        return PsxBusAccessReason::ok;
    }

    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset_u16(address, offset);
    if (reason != PsxBusAccessReason::ok) return reason;

    bus.ram[offset + 0u] = static_cast<std::uint8_t>(value);
    bus.ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_write_u32(PsxBus& bus,
                                                           std::uint32_t address,
                                                           std::uint32_t value) noexcept {
    if ((address & 3u) != 0u) return PsxBusAccessReason::misaligned;
    if (address == PsxBus::common_delay_address) {
        bus.common_delay = static_cast<std::uint16_t>(value);
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::interrupt_status_address) {
        return psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
    }
    if (address == PsxBus::interrupt_mask_address) {
        return psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
    }
    if (address == PsxBus::dma2_channel_control_address) {
        if ((value & PsxBus::dma_channel_start_busy) != 0u) {
            return PsxBusAccessReason::unmapped;
        }
        bus.dma2_channel_control = value & PsxBus::dma2_channel_control_mask;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::dma_control_address) {
        bus.dma_control = value;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::dma_interrupt_address) {
        const bool previous_master =
            (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        const auto previous_flags = bus.dma_interrupt & PsxBus::dma_interrupt_flag_mask;
        const auto acknowledged = value & PsxBus::dma_interrupt_flag_mask;
        const auto remaining_flags = previous_flags & ~acknowledged;
        const auto control = value & PsxBus::dma_interrupt_control_mask;
        bus.dma_interrupt = control | remaining_flags;
        const bool current_master =
            (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        if (!previous_master && current_master) {
            bus.interrupt_status = static_cast<std::uint16_t>(
                bus.interrupt_status | (1u << 3u));
        }
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer1_mode_address) {
        return psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
    }
    if (address == PsxBus::gpu_gp0_address) {
        bus.gpu_gp0_write_latch = value;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::gpu_gp1_address) {
        const auto command = static_cast<std::uint8_t>(value >> 24u);
        if (command == 0x00u) {
            bus.gpu_status = PsxBus::gpu_status_reset;
            return PsxBusAccessReason::ok;
        }
        if (command == 0x10u) {
            const auto index = value & 0x00ffffffu;
            if (index != 0x07u) return PsxBusAccessReason::unmapped;
            bus.gpu_read_latch = 2u;
            return PsxBusAccessReason::ok;
        }
        if (command != 0x03u) return PsxBusAccessReason::unmapped;
        if ((value & 1u) != 0u) {
            bus.gpu_status |= PsxBus::gpu_status_display_disabled;
        } else {
            bus.gpu_status &= ~PsxBus::gpu_status_display_disabled;
        }
        return PsxBusAccessReason::ok;
    }

    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 4u, scratchpad_offset) ==
        PsxBusAccessReason::ok) {
        bus.scratchpad[scratchpad_offset + 0u] = static_cast<std::uint8_t>(value);
        bus.scratchpad[scratchpad_offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        bus.scratchpad[scratchpad_offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
        bus.scratchpad[scratchpad_offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
        return PsxBusAccessReason::ok;
    }

    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset(address, offset);
    if (reason != PsxBusAccessReason::ok) return reason;

    bus.ram[offset + 0u] = static_cast<std::uint8_t>(value);
    bus.ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bus.ram[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bus.ram[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    return PsxBusAccessReason::ok;
}

}
