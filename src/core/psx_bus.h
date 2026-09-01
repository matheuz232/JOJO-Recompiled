#pragma once
#include <algorithm>
#include "core/psx_sio0.h"
#include "core/result.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <system_error>
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
    static constexpr std::uint32_t spu_delay_address = 0x1f801014u;
    static constexpr std::uint32_t common_delay_address = 0x1f801020u;
    static constexpr std::uint32_t sio0_data_address = 0x1f801040u;
    static constexpr std::uint32_t sio0_status_address = 0x1f801044u;
    static constexpr std::uint32_t sio0_mode_address = 0x1f801048u;
    static constexpr std::uint32_t sio0_control_address = 0x1f80104au;
    static constexpr std::uint32_t sio0_baud_address = 0x1f80104eu;
    static constexpr std::uint32_t interrupt_status_address = 0x1f801070u;
    static constexpr std::uint32_t interrupt_mask_address = 0x1f801074u;
    static constexpr std::uint16_t cdrom_interrupt_request = 1u << 2u;
    static constexpr std::uint32_t dma2_base_address = 0x1f8010a0u;
    static constexpr std::uint32_t dma2_block_control_address = 0x1f8010a4u;
    static constexpr std::uint32_t dma2_channel_control_address = 0x1f8010a8u;
    static constexpr std::uint32_t dma3_base_address = 0x1f8010b0u;
    static constexpr std::uint32_t dma3_block_control_address = 0x1f8010b4u;
    static constexpr std::uint32_t dma3_channel_control_address = 0x1f8010b8u;
    static constexpr std::uint32_t dma4_base_address = 0x1f8010c0u;
    static constexpr std::uint32_t dma4_block_control_address = 0x1f8010c4u;
    static constexpr std::uint32_t dma4_channel_control_address = 0x1f8010c8u;
    static constexpr std::uint32_t dma6_base_address = 0x1f8010e0u;
    static constexpr std::uint32_t dma6_block_control_address = 0x1f8010e4u;
    static constexpr std::uint32_t dma6_channel_control_address = 0x1f8010e8u;
    static constexpr std::uint32_t dma_control_address = 0x1f8010f0u;
    static constexpr std::uint32_t dma_interrupt_address = 0x1f8010f4u;
    static constexpr std::uint32_t timer0_current_address = 0x1f801100u;
    static constexpr std::uint32_t timer0_mode_address = 0x1f801104u;
    static constexpr std::uint32_t timer0_target_address = 0x1f801108u;
    static constexpr std::uint32_t timer1_current_address = 0x1f801110u;
    static constexpr std::uint32_t timer1_mode_address = 0x1f801114u;
    static constexpr std::uint32_t timer1_target_address = 0x1f801118u;
    static constexpr std::uint32_t timer2_current_address = 0x1f801120u;
    static constexpr std::uint32_t timer2_mode_address = 0x1f801124u;
    static constexpr std::uint32_t timer2_target_address = 0x1f801128u;
    static constexpr std::uint32_t cdrom_base_address = 0x1f801800u;
    static constexpr std::uint32_t cdrom_end_address = cdrom_base_address + 3u;
    static constexpr std::uint8_t cdrom_bank_mask = 0x03u;
    static constexpr std::uint8_t cdrom_interrupt_bits = 0x1fu;
    static constexpr std::uint8_t cdrom_hc05_interrupt_bits = 0x07u;
    static constexpr std::uint8_t cdrom_read_reserved_bits = 0xe0u;
    static constexpr std::uint8_t cdrom_hsts_parameter_empty = 1u << 3u;
    static constexpr std::uint8_t cdrom_hsts_parameter_write_ready = 1u << 4u;
    static constexpr std::uint8_t cdrom_hsts_result_ready = 1u << 5u;
    static constexpr std::uint8_t cdrom_hsts_data_request = 1u << 6u;
    static constexpr std::uint8_t cdrom_clear_parameters = 1u << 6u;
    static constexpr std::uint8_t cdrom_decoder_reset = 1u << 7u;
    static constexpr std::uint8_t cdrom_sound_map_clear = 1u << 5u;
    static constexpr std::size_t cdrom_parameter_capacity = 16u;
    static constexpr std::size_t cdrom_result_capacity = 16u;
    static constexpr std::size_t cdrom_data_sector_size = 2048u;
    static constexpr std::uint8_t cdrom_command_nop = 0x01u;
    static constexpr std::uint8_t cdrom_command_init = 0x0au;
    static constexpr std::uint8_t cdrom_command_demute = 0x0cu;
    static constexpr std::uint8_t cdrom_command_setloc = 0x02u;
    static constexpr std::uint8_t cdrom_command_readn = 0x06u;
    static constexpr std::uint8_t cdrom_command_pause = 0x09u;
    static constexpr std::uint8_t cdrom_command_setfilter = 0x0du;
    static constexpr std::uint8_t cdrom_command_setmode = 0x0eu;
    static constexpr std::uint8_t cdrom_command_getparam = 0x0fu;
    static constexpr std::uint8_t cdrom_interrupt_data_ready = 0x01u;
    static constexpr std::uint8_t cdrom_interrupt_complete = 0x02u;
    static constexpr std::uint8_t cdrom_interrupt_acknowledge = 0x03u;
    static constexpr std::uint8_t cdrom_interrupt_data_end = 0x04u;
    static constexpr std::uint8_t cdrom_interrupt_error = 0x05u;
    static constexpr std::uint8_t cdrom_status_motor_on = 1u << 1u;
    static constexpr std::uint32_t gpu_gp0_address = 0x1f801810u;
    static constexpr std::uint32_t gpu_gp1_address = 0x1f801814u;
    static constexpr std::uint32_t spu_register_base = 0x1f801c00u;
    static constexpr std::uint32_t spu_register_end = 0x1f801dffu;
    static constexpr std::size_t spu_register_count = 0x200u / 2u;
    static constexpr std::size_t spu_ram_size = 512u * 1024u;
    static constexpr std::uint32_t spu_transfer_address_register = 0x1f801da6u;
    static constexpr std::uint32_t gpu_status_reset = 0x14802000u;
    static constexpr std::uint32_t gpu_status_display_disabled = 1u << 23u;
    static constexpr std::uint32_t gpu_horizontal_display_range_reset =
        0x200u | ((0x200u + 256u * 10u) << 12u);
    static constexpr std::uint32_t gpu_vertical_display_range_reset =
        0x010u | ((0x010u + 240u) << 10u);
    static constexpr std::size_t gpu_vram_width = 1024u;
    static constexpr std::size_t gpu_vram_height = 512u;
    static constexpr std::uint16_t interrupt_status_valid_bits = 0x07ffu;
    static constexpr std::uint16_t interrupt_mask_valid_bits = 0x07ffu;
    static constexpr std::uint16_t timer_mode_guest_bits = 0x1fffu;
    static constexpr std::uint16_t timer_mode_write_epoch = 0x8000u;
    static constexpr std::uint16_t timer_mode_valid_bits =
        timer_mode_guest_bits | timer_mode_write_epoch;
    static constexpr std::uint32_t dma2_channel_control_mask = 0x71770703u;
    static constexpr std::uint32_t dma_channel_start_busy = 1u << 24u;
    static constexpr std::uint32_t dma_interrupt_control_mask = 0x00ff807fu;
    static constexpr std::uint32_t dma_interrupt_flag_mask = 0x7f000000u;
    static constexpr std::uint32_t dma_interrupt_master_enable = 0x00800000u;
    static constexpr std::uint32_t dma_interrupt_bus_error = 0x00008000u;
    static constexpr std::uint32_t dma_interrupt_master_flag = 0x80000000u;

    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(main_ram_size, 0u);
    std::array<std::uint8_t, scratchpad_size> scratchpad{};
    std::uint32_t spu_delay{0x200931e1u};
    std::uint16_t common_delay{};
    PsxSio0State sio0{};
    std::uint16_t interrupt_status{};
    std::uint16_t interrupt_mask{};
    std::uint32_t dma4_base{};
    std::uint32_t dma4_block_control{};
    std::uint32_t dma4_channel_control{};
    std::uint32_t dma2_base{};
    std::uint32_t dma2_block_control{};
    std::uint32_t dma2_channel_control{};
    std::uint32_t dma3_base{};
    std::uint32_t dma3_block_control{};
    std::uint32_t dma3_channel_control{};
    std::uint32_t dma6_base{};
    std::uint32_t dma6_block_control{};
    std::uint32_t dma6_channel_control{};
    std::uint32_t dma_control{};
    std::uint32_t dma_interrupt{};
    std::uint16_t timer0_current{};
    std::uint16_t timer0_mode{};
    std::uint16_t timer0_target{};
    std::uint16_t timer1_current{};
    std::uint16_t timer1_mode{};
    std::uint16_t timer1_target{};
    std::uint16_t timer2_current{};
    std::uint16_t timer2_mode{};
    std::uint16_t timer2_target{};
    bool timer0_reset_pending{};
    bool timer1_reset_pending{};
    bool timer2_reset_pending{};
    bool timer0_irq_fired{};
    bool timer1_irq_fired{};
    bool timer2_irq_fired{};
    std::uint8_t timer2_clock_phase{};
    std::uint64_t video_clock_phase{};
    std::uint16_t gpu_scanline{};
    std::uint8_t cdrom_index{};
    std::uint8_t cdrom_interrupt_enable{};
    std::uint8_t cdrom_interrupt_flags{};
    std::uint8_t cdrom_host_control{};
    std::array<std::uint8_t, cdrom_parameter_capacity> cdrom_parameter_fifo{};
    std::uint8_t cdrom_parameter_count{};
    std::array<std::uint8_t, cdrom_result_capacity> cdrom_result_fifo{};
    std::uint8_t cdrom_result_read_index{};
    std::uint8_t cdrom_result_count{};
    std::uint8_t cdrom_mode{};
    std::uint8_t cdrom_filter_file{};
    std::uint8_t cdrom_filter_channel{};
    std::uint8_t cdrom_location_minute_bcd{};
    std::uint8_t cdrom_location_second_bcd{0x02u};
    std::uint8_t cdrom_location_sector_bcd{};
    std::uint32_t cdrom_location_lba{};
    bool cdrom_reading{};
    std::array<std::uint8_t, cdrom_data_sector_size> cdrom_sector_buffer{};
    std::uint16_t cdrom_data_read_index{};
    std::uint16_t cdrom_data_count{};
    bool cdrom_sector_buffer_ready{};
    std::filesystem::path cdrom_image_path{};
    std::uint64_t cdrom_image_sector_count{};
    bool cdrom_image_mounted{};
    std::uint8_t cdrom_status{cdrom_status_motor_on};
    std::uint32_t cdrom_async_cycles{};
    std::uint8_t cdrom_async_interrupt{};
    bool cdrom_muted{};
    std::uint8_t cdrom_volume_left_to_left{};
    std::uint8_t cdrom_volume_left_to_right{};
    std::uint8_t cdrom_volume_right_to_right{};
    std::uint8_t cdrom_volume_right_to_left{};
    std::uint8_t cdrom_volume_pending_left_to_left{};
    std::uint8_t cdrom_volume_pending_left_to_right{};
    std::uint8_t cdrom_volume_pending_right_to_right{};
    std::uint8_t cdrom_volume_pending_right_to_left{};
    std::uint32_t gpu_gp0_write_latch{};
    std::uint32_t gpu_read_latch{};
    std::uint32_t gpu_status{gpu_status_reset};
    std::uint32_t gpu_draw_mode{};
    std::uint32_t gpu_texture_window{};
    std::uint32_t gpu_drawing_area_top_left{};
    std::uint32_t gpu_drawing_area_bottom_right{};
    std::uint32_t gpu_drawing_offset{};
    std::uint32_t gpu_display_vram_start{};
    std::uint32_t gpu_horizontal_display_range{gpu_horizontal_display_range_reset};
    std::uint32_t gpu_vertical_display_range{gpu_vertical_display_range_reset};
    std::uint8_t gpu_display_mode{};
    std::vector<std::uint16_t> gpu_vram =
        std::vector<std::uint16_t>(gpu_vram_width * gpu_vram_height, 0u);
    std::array<std::uint32_t, 12> gpu_gp0_packet{};
    std::uint8_t gpu_gp0_packet_count{};
    std::uint8_t gpu_gp0_packet_words{};
    bool gpu_polyline_active{};
    bool gpu_polyline_gouraud{};
    bool gpu_polyline_expect_color{};
    std::array<std::uint16_t, spu_register_count> spu_registers{};
    std::vector<std::uint8_t> spu_ram = std::vector<std::uint8_t>(spu_ram_size, 0u);
};

[[nodiscard]] inline std::uint16_t psx_gpu_bgr555(std::uint32_t rgb24) noexcept {
    const auto red = static_cast<std::uint16_t>((rgb24 >> 3u) & 0x1fu);
    const auto green = static_cast<std::uint16_t>((rgb24 >> 11u) & 0x1fu);
    const auto blue = static_cast<std::uint16_t>((rgb24 >> 19u) & 0x1fu);
    return static_cast<std::uint16_t>(red | (green << 5u) | (blue << 10u));
}

[[nodiscard]] inline std::int32_t psx_gpu_sign_extend_11(std::uint32_t value) noexcept {
    value &= 0x7ffu;
    return (value & 0x400u) != 0u
               ? static_cast<std::int32_t>(value) - 0x800
               : static_cast<std::int32_t>(value);
}

[[nodiscard]] inline std::uint16_t psx_gpu_blend_bgr555(
    std::uint16_t back,
    std::uint16_t front,
    std::uint32_t mode) noexcept {
    const auto blend_channel = [mode](std::uint32_t b, std::uint32_t f) noexcept {
        switch (mode & 3u) {
        case 0u:
            return (b >> 1u) + (f >> 1u);
        case 1u:
            return std::min<std::uint32_t>(31u, b + f);
        case 2u:
            return b > f ? b - f : 0u;
        default:
            return std::min<std::uint32_t>(31u, b + (f >> 2u));
        }
    };

    const auto red = blend_channel(back & 0x1fu, front & 0x1fu);
    const auto green = blend_channel((back >> 5u) & 0x1fu, (front >> 5u) & 0x1fu);
    const auto blue = blend_channel((back >> 10u) & 0x1fu, (front >> 10u) & 0x1fu);
    return static_cast<std::uint16_t>(
        (front & 0x8000u) | red | (green << 5u) | (blue << 10u));
}

inline void psx_gpu_write_render_pixel(PsxBus& bus,
                                       std::int32_t x,
                                       std::int32_t y,
                                       std::uint16_t pixel,
                                       bool semi_transparent = false) noexcept {
    if (x < 0 || y < 0 ||
        x >= static_cast<std::int32_t>(PsxBus::gpu_vram_width) ||
        y >= static_cast<std::int32_t>(PsxBus::gpu_vram_height)) {
        return;
    }

    const auto draw_x1 = static_cast<std::int32_t>(
        bus.gpu_drawing_area_top_left & 0x3ffu);
    const auto draw_y1 = static_cast<std::int32_t>(
        (bus.gpu_drawing_area_top_left >> 10u) & 0x1ffu);
    const auto draw_x2 = static_cast<std::int32_t>(
        bus.gpu_drawing_area_bottom_right & 0x3ffu);
    const auto draw_y2 = static_cast<std::int32_t>(
        (bus.gpu_drawing_area_bottom_right >> 10u) & 0x1ffu);
    if (draw_x2 < draw_x1 || draw_y2 < draw_y1 ||
        x < draw_x1 || x > draw_x2 || y < draw_y1 || y > draw_y2) {
        return;
    }

    auto& destination = bus.gpu_vram[
        static_cast<std::size_t>(y) * PsxBus::gpu_vram_width +
        static_cast<std::size_t>(x)];
    const bool check_mask = (bus.gpu_status & (1u << 12u)) != 0u;
    if (check_mask && (destination & 0x8000u) != 0u) return;
    if (semi_transparent) {
        pixel = psx_gpu_blend_bgr555(
            destination, pixel, (bus.gpu_draw_mode >> 5u) & 3u);
    }
    if ((bus.gpu_status & (1u << 11u)) != 0u) {
        pixel = static_cast<std::uint16_t>(pixel | 0x8000u);
    }
    destination = pixel;
}

#include "core/psx_gpu_texture.inl"
#include "core/psx_gpu_polygon.inl"
#include "core/psx_gpu_line.inl"
#include "core/psx_gpu_transfer.inl"

inline void psx_gpu_execute_render_rectangle(PsxBus& bus) noexcept {
    const auto command = bus.gpu_gp0_packet[0];
    const auto position = bus.gpu_gp0_packet[1];
    const auto size_mode = (command >> 27u) & 3u;

    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    switch (size_mode) {
    case 0u:
        width = bus.gpu_gp0_packet[2] & 0xffffu;
        height = bus.gpu_gp0_packet[2] >> 16u;
        if (width > 1023u || height > 511u) return;
        break;
    case 1u:
        width = 1u;
        height = 1u;
        break;
    case 2u:
        width = 8u;
        height = 8u;
        break;
    default:
        width = 16u;
        height = 16u;
        break;
    }
    if (width == 0u || height == 0u) return;

    const auto offset_x = psx_gpu_sign_extend_11(bus.gpu_drawing_offset);
    const auto offset_y = psx_gpu_sign_extend_11(bus.gpu_drawing_offset >> 11u);
    const auto origin_x = psx_gpu_sign_extend_11(position) + offset_x;
    const auto origin_y = psx_gpu_sign_extend_11(position >> 16u) + offset_y;
    const auto color = psx_gpu_bgr555(command & 0x00ffffffu);
    const bool semi_transparent = (command & (1u << 25u)) != 0u;

    for (std::uint32_t row = 0u; row < height; ++row) {
        for (std::uint32_t column = 0u; column < width; ++column) {
            psx_gpu_write_render_pixel(
                bus,
                origin_x + static_cast<std::int32_t>(column),
                origin_y + static_cast<std::int32_t>(row),
                color,
                semi_transparent);
        }
    }
}

inline void psx_gpu_execute_fill_rectangle(PsxBus& bus) noexcept {
    const auto color = psx_gpu_bgr555(bus.gpu_gp0_packet[0] & 0x00ffffffu);
    const auto position = bus.gpu_gp0_packet[1];
    const auto size = bus.gpu_gp0_packet[2];
    const auto x = static_cast<std::size_t>(position & 0x3ffu);
    const auto y = static_cast<std::size_t>((position >> 16u) & 0x1ffu);
    const auto width = static_cast<std::size_t>(size & 0x3ffu);
    const auto height = static_cast<std::size_t>((size >> 16u) & 0x1ffu);
    const auto end_x = std::min(x + width, PsxBus::gpu_vram_width);
    const auto end_y = std::min(y + height, PsxBus::gpu_vram_height);
    for (auto row = y; row < end_y; ++row) {
        for (auto column = x; column < end_x; ++column) {
            bus.gpu_vram[row * PsxBus::gpu_vram_width + column] = color;
        }
    }
}

inline void psx_gpu_write_gp0(PsxBus& bus, std::uint32_t value) noexcept {
    constexpr std::uint8_t image_payload_state = 0xffu;
    bus.gpu_gp0_write_latch = value;

    if (psx_gpu_vram_to_cpu_active(bus)) return;
    if (psx_gpu_consume_polyline_word(bus, value)) return;

    if (bus.gpu_gp0_packet_count == image_payload_state) {
        const auto destination = bus.gpu_gp0_packet[0];
        const auto dimensions = bus.gpu_gp0_packet[1];
        auto written = bus.gpu_gp0_packet[2];
        const auto base_x = destination & 0x3ffu;
        const auto base_y = (destination >> 16u) & 0x1ffu;
        const auto width = dimensions & 0xffffu;
        const auto height = dimensions >> 16u;
        const auto total = width * height;
        const bool force_mask = (bus.gpu_status & (1u << 11u)) != 0u;
        const bool check_mask = (bus.gpu_status & (1u << 12u)) != 0u;

        for (std::uint32_t half = 0u; half < 2u && written < total; ++half) {
            auto pixel = static_cast<std::uint16_t>(value >> (half * 16u));
            const auto local_x = written % width;
            const auto local_y = written / width;
            const auto x = (base_x + local_x) & 0x3ffu;
            const auto y = (base_y + local_y) & 0x1ffu;
            auto& destination_pixel =
                bus.gpu_vram[static_cast<std::size_t>(y) * PsxBus::gpu_vram_width + x];
            if (!check_mask || (destination_pixel & 0x8000u) == 0u) {
                if (force_mask) pixel = static_cast<std::uint16_t>(pixel | 0x8000u);
                destination_pixel = pixel;
            }
            ++written;
        }
        bus.gpu_gp0_packet[2] = written;
        if (written >= total) {
            bus.gpu_gp0_packet = {};
            bus.gpu_gp0_packet_count = 0u;
            bus.gpu_gp0_packet_words = 0u;
        }
        return;
    }

    if (bus.gpu_gp0_packet_count == 0u) {
        const auto command = static_cast<std::uint8_t>(value >> 24u);
        switch (command) {
        case 0x1fu: {
            const bool was_requested = (bus.gpu_status & (1u << 24u)) != 0u;
            bus.gpu_status |= 1u << 24u;
            if (!was_requested) {
                bus.interrupt_status = static_cast<std::uint16_t>(
                    bus.interrupt_status | (1u << 1u));
            }
            return;
        }
        case 0xe1u:
            bus.gpu_draw_mode = value & 0x00003fffu;
            bus.gpu_status =
                (bus.gpu_status & ~(0x000007ffu | (1u << 15u))) |
                (value & 0x000007ffu) |
                (((value >> 11u) & 1u) << 15u);
            return;
        case 0xe2u:
            bus.gpu_texture_window = value & 0x000fffffu;
            return;
        case 0xe3u:
            bus.gpu_drawing_area_top_left = value & 0x0007ffffu;
            return;
        case 0xe4u:
            bus.gpu_drawing_area_bottom_right = value & 0x0007ffffu;
            return;
        case 0xe5u:
            bus.gpu_drawing_offset = value & 0x003fffffu;
            return;
        case 0xe6u:
            bus.gpu_status = (bus.gpu_status & ~(3u << 11u)) |
                             ((value & 3u) << 11u);
            return;
        default:
            break;
        }

        const bool vram_to_vram = (value >> 29u) == 4u;
        const bool cpu_to_vram = (value >> 29u) == 5u;
        const bool vram_to_cpu = (value >> 29u) == 6u;
        const bool polygon = (value >> 29u) == 1u;
        const bool line = (value >> 29u) == 2u;
        const bool rectangle = (value >> 29u) == 3u;
        const bool gouraud = (value & (1u << 28u)) != 0u;
        const bool quad = (value & (1u << 27u)) != 0u;
        const bool textured = (value & (1u << 26u)) != 0u;
        if (command == 0x02u || cpu_to_vram || vram_to_cpu) {
            bus.gpu_gp0_packet_words = 3u;
        } else if (vram_to_vram) {
            bus.gpu_gp0_packet_words = 4u;
        } else if (polygon) {
            if (textured) {
                if (gouraud) {
                    bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(quad ? 12u : 9u);
                } else {
                    bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(quad ? 9u : 7u);
                }
            } else if (gouraud) {
                bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(quad ? 8u : 6u);
            } else {
                bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(quad ? 5u : 4u);
            }
        } else if (line) {
            bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(gouraud ? 4u : 3u);
        } else if (rectangle) {
            const auto size_mode = (value >> 27u) & 3u;
            if (textured) {
                bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(
                    size_mode == 0u ? 4u : 3u);
            } else {
                bus.gpu_gp0_packet_words = static_cast<std::uint8_t>(
                    size_mode == 0u ? 3u : 2u);
            }
        } else {
            return;
        }
    }

    bus.gpu_gp0_packet[bus.gpu_gp0_packet_count++] = value;
    if (bus.gpu_gp0_packet_count != bus.gpu_gp0_packet_words) return;

    const auto transfer_group = bus.gpu_gp0_packet[0] >> 29u;
    if (transfer_group == 4u) {
        psx_gpu_execute_vram_to_vram(bus);
        bus.gpu_gp0_packet_count = 0u;
        bus.gpu_gp0_packet_words = 0u;
        return;
    }
    if (transfer_group == 6u) {
        psx_gpu_begin_vram_to_cpu(bus);
        return;
    }
    if (transfer_group == 5u) {
        const auto destination = bus.gpu_gp0_packet[1];
        const auto raw_size = bus.gpu_gp0_packet[2];
        const auto raw_width = raw_size & 0xffffu;
        const auto raw_height = raw_size >> 16u;
        const auto width = ((raw_width - 1u) & 0x3ffu) + 1u;
        const auto height = ((raw_height - 1u) & 0x1ffu) + 1u;
        bus.gpu_gp0_packet[0] = destination;
        bus.gpu_gp0_packet[1] = width | (height << 16u);
        bus.gpu_gp0_packet[2] = 0u;
        bus.gpu_gp0_packet_count = image_payload_state;
        bus.gpu_gp0_packet_words = 0u;
        return;
    }

    const auto primitive_group = bus.gpu_gp0_packet[0] >> 29u;
    if (primitive_group == 1u) {
        if ((bus.gpu_gp0_packet[0] & (1u << 26u)) != 0u) {
            psx_gpu_execute_textured_polygon(bus);
        } else if ((bus.gpu_gp0_packet[0] & (1u << 28u)) != 0u) {
            psx_gpu_execute_gouraud_polygon(bus);
        } else {
            psx_gpu_execute_flat_polygon(bus);
        }
    } else if (primitive_group == 2u) {
        psx_gpu_execute_line(bus);
        if ((bus.gpu_gp0_packet[0] & (1u << 27u)) != 0u) {
            const bool gouraud = (bus.gpu_gp0_packet[0] & (1u << 28u)) != 0u;
            bus.gpu_polyline_active = true;
            bus.gpu_polyline_gouraud = gouraud;
            bus.gpu_polyline_expect_color = gouraud;
            if (gouraud) {
                bus.gpu_gp0_packet[1] = bus.gpu_gp0_packet[3];
            } else {
                bus.gpu_gp0_packet[1] = bus.gpu_gp0_packet[2];
            }
            bus.gpu_gp0_packet_count = 0u;
            bus.gpu_gp0_packet_words = 0u;
            return;
        }
    } else if (primitive_group == 3u) {
        if ((bus.gpu_gp0_packet[0] & (1u << 26u)) != 0u) {
            psx_gpu_execute_textured_rectangle(bus);
        } else {
            psx_gpu_execute_render_rectangle(bus);
        }
    } else {
        psx_gpu_execute_fill_rectangle(bus);
    }
    bus.gpu_gp0_packet_count = 0u;
    bus.gpu_gp0_packet_words = 0u;
}

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

inline void psx_bus_latch_cdrom_irq_rising_edge(PsxBus& bus, bool was_active) noexcept {
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

inline void psx_bus_cdrom_reset_results(PsxBus& bus) noexcept {
    bus.cdrom_result_read_index = 0u;
    bus.cdrom_result_count = 0u;
}

inline void psx_bus_cdrom_clear_parameters(PsxBus& bus) noexcept {
    bus.cdrom_parameter_count = 0u;
}

[[nodiscard]] inline bool psx_bus_cdrom_raise_interrupt(
    PsxBus& bus,
    std::uint8_t interrupt,
    std::initializer_list<std::uint8_t> results) noexcept {
    if (results.size() > PsxBus::cdrom_result_capacity) return false;
    const bool was_active = psx_bus_cdrom_irq_active(bus);
    psx_bus_cdrom_reset_results(bus);
    for (const auto value : results) {
        if (!psx_bus_cdrom_push_result(bus, value)) return false;
    }
    bus.cdrom_interrupt_flags = static_cast<std::uint8_t>(
        (bus.cdrom_interrupt_flags &
         static_cast<std::uint8_t>(~PsxBus::cdrom_hc05_interrupt_bits)) |
        (interrupt & PsxBus::cdrom_hc05_interrupt_bits));
    psx_bus_latch_cdrom_irq_rising_edge(bus, was_active);
    return true;
}

[[nodiscard]] inline bool psx_bus_cdrom_valid_bcd(std::uint8_t value) noexcept {
    return (value & 0x0fu) <= 9u && ((value >> 4u) & 0x0fu) <= 9u;
}

[[nodiscard]] inline std::uint32_t psx_bus_cdrom_bcd_to_binary(std::uint8_t value) noexcept {
    return static_cast<std::uint32_t>((value >> 4u) * 10u + (value & 0x0fu));
}

[[nodiscard]] inline bool psx_bus_cdrom_set_location(PsxBus& bus) noexcept {
    if (bus.cdrom_parameter_count != 3u) return false;
    const auto minute = bus.cdrom_parameter_fifo[0];
    const auto second = bus.cdrom_parameter_fifo[1];
    const auto sector = bus.cdrom_parameter_fifo[2];
    if (!psx_bus_cdrom_valid_bcd(minute) ||
        !psx_bus_cdrom_valid_bcd(second) ||
        !psx_bus_cdrom_valid_bcd(sector)) {
        return false;
    }
    const auto mm = psx_bus_cdrom_bcd_to_binary(minute);
    const auto ss = psx_bus_cdrom_bcd_to_binary(second);
    const auto ff = psx_bus_cdrom_bcd_to_binary(sector);
    if (ss >= 60u || ff >= 75u) return false;
    const auto absolute_frame = (mm * 60u + ss) * 75u + ff;
    if (absolute_frame < 150u) return false;
    bus.cdrom_location_minute_bcd = minute;
    bus.cdrom_location_second_bcd = second;
    bus.cdrom_location_sector_bcd = sector;
    bus.cdrom_location_lba = absolute_frame - 150u;
    return true;
}

[[nodiscard]] inline bool psx_bus_cdrom_execute_command(PsxBus& bus,
                                                         std::uint8_t command) noexcept {
    if (command == PsxBus::cdrom_command_init ||
        command == PsxBus::cdrom_command_demute) {
        if (bus.cdrom_parameter_count != 0u ||
            (bus.cdrom_interrupt_flags & PsxBus::cdrom_hc05_interrupt_bits) != 0u) {
            return false;
        }
        const bool was_active = psx_bus_cdrom_irq_active(bus);
        bus.cdrom_result_read_index = 0u;
        if (!psx_bus_cdrom_push_result(bus, bus.cdrom_status)) return false;
        bus.cdrom_interrupt_flags = static_cast<std::uint8_t>(
            (bus.cdrom_interrupt_flags &
             static_cast<std::uint8_t>(~PsxBus::cdrom_hc05_interrupt_bits)) |
            PsxBus::cdrom_interrupt_acknowledge);
        if (command == PsxBus::cdrom_command_init) {
            constexpr std::uint32_t init_completion_cycles = 33'869u;
            bus.cdrom_async_cycles = init_completion_cycles;
            bus.cdrom_async_interrupt = PsxBus::cdrom_interrupt_complete;
        } else {
            bus.cdrom_muted = false;
        }
        psx_bus_cdrom_clear_parameters(bus);
        psx_bus_latch_cdrom_irq_rising_edge(bus, was_active);
        return true;
    }

    if ((bus.cdrom_interrupt_flags & PsxBus::cdrom_hc05_interrupt_bits) != 0u ||
        bus.cdrom_result_count != 0u) {
        return false;
    }

    switch (command) {
    case PsxBus::cdrom_command_nop:
        if (bus.cdrom_parameter_count != 0u) return false;
        if (!psx_bus_cdrom_raise_interrupt(
                bus, PsxBus::cdrom_interrupt_acknowledge, {bus.cdrom_status})) {
            return false;
        }
        break;
    case PsxBus::cdrom_command_setloc:
        if (!psx_bus_cdrom_set_location(bus)) return false;
        if (!psx_bus_cdrom_raise_interrupt(
                bus, PsxBus::cdrom_interrupt_acknowledge, {bus.cdrom_status})) {
            return false;
        }
        break;
    case PsxBus::cdrom_command_readn:
        if (bus.cdrom_parameter_count != 0u || (bus.cdrom_mode & 0x20u) != 0u) {
            return false;
        }
        bus.cdrom_reading = true;
        bus.cdrom_sector_buffer_ready = false;
        bus.cdrom_data_read_index = 0u;
        bus.cdrom_data_count = 0u;
        if (!psx_bus_cdrom_raise_interrupt(
                bus, PsxBus::cdrom_interrupt_acknowledge, {bus.cdrom_status})) {
            return false;
        }
        break;
    case PsxBus::cdrom_command_pause:
        if (bus.cdrom_parameter_count != 0u) return false;
        bus.cdrom_reading = false;
        bus.cdrom_sector_buffer_ready = false;
        bus.cdrom_data_read_index = 0u;
        bus.cdrom_data_count = 0u;
        if (!psx_bus_cdrom_raise_interrupt(
                bus, PsxBus::cdrom_interrupt_acknowledge, {bus.cdrom_status})) {
            return false;
        }
        break;
    case PsxBus::cdrom_command_setfilter:
        if (bus.cdrom_parameter_count != 2u) return false;
        bus.cdrom_filter_file = bus.cdrom_parameter_fifo[0];
        bus.cdrom_filter_channel = bus.cdrom_parameter_fifo[1];
        if (!psx_bus_cdrom_raise_interrupt(
                bus, PsxBus::cdrom_interrupt_acknowledge, {bus.cdrom_status})) {
            return false;
        }
        break;
    case PsxBus::cdrom_command_setmode:
        if (bus.cdrom_parameter_count != 1u) return false;
        bus.cdrom_mode = bus.cdrom_parameter_fifo[0];
        if (!psx_bus_cdrom_raise_interrupt(
                bus, PsxBus::cdrom_interrupt_acknowledge, {bus.cdrom_status})) {
            return false;
        }
        break;
    case PsxBus::cdrom_command_getparam:
        if (bus.cdrom_parameter_count != 0u) return false;
        if (!psx_bus_cdrom_raise_interrupt(
                bus,
                PsxBus::cdrom_interrupt_acknowledge,
                {bus.cdrom_status,
                 bus.cdrom_mode,
                 0u,
                 bus.cdrom_filter_file,
                 bus.cdrom_filter_channel})) {
            return false;
        }
        break;
    default:
        return false;
    }
    psx_bus_cdrom_clear_parameters(bus);
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

[[nodiscard]] inline PsxBusAccessReason psx_bus_ram_offset_u16(
    std::uint32_t address, std::size_t& offset) noexcept {
    if ((address & 1u) != 0u) return PsxBusAccessReason::misaligned;
    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) return PsxBusAccessReason::unmapped;
    if (physical >= PsxBus::default_ram_mirror_window) return PsxBusAccessReason::unmapped;
    offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_ram_offset(
    std::uint32_t address, std::size_t& offset) noexcept {
    if ((address & 3u) != 0u) return PsxBusAccessReason::misaligned;
    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) return PsxBusAccessReason::unmapped;
    if (physical >= PsxBus::default_ram_mirror_window) return PsxBusAccessReason::unmapped;
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
    if (bus.cdrom_data_count != 0u) {
        value = static_cast<std::uint8_t>(value | PsxBus::cdrom_hsts_data_request);
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
    if (physical == PsxBus::sio0_data_address) {
        const auto value = bus.sio0.rx_count == 0u
                               ? 0xffu
                               : bus.sio0.rx_fifo[static_cast<std::size_t>(bus.sio0.rx_read_index)];
        return {PsxBusAccessReason::ok, static_cast<std::uint8_t>(value)};
    }
    if (physical == PsxBus::cdrom_base_address) {
        return {PsxBusAccessReason::ok, psx_bus_cdrom_hsts(bus)};
    }
    if (physical == PsxBus::cdrom_base_address + 1u && bus.cdrom_result_count != 0u) {
        return {PsxBusAccessReason::ok,
                bus.cdrom_result_fifo[static_cast<std::size_t>(bus.cdrom_result_read_index)]};
    }
    if (physical == PsxBus::cdrom_base_address + 2u && bus.cdrom_data_count != 0u) {
        return {PsxBusAccessReason::ok,
                bus.cdrom_sector_buffer[static_cast<std::size_t>(bus.cdrom_data_read_index)]};
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
        physical == PsxBus::sio0_data_address) {
        return {PsxBusAccessReason::ok, psx_sio0_read_data(bus.sio0)};
    }
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
    if (psx_bus_virtual_to_physical(address, physical) &&
        physical == PsxBus::cdrom_base_address + 2u &&
        bus.cdrom_data_count != 0u) {
        const auto value = bus.cdrom_sector_buffer[
            static_cast<std::size_t>(bus.cdrom_data_read_index)];
        ++bus.cdrom_data_read_index;
        --bus.cdrom_data_count;
        if (bus.cdrom_data_count == 0u) bus.cdrom_data_read_index = 0u;
        return {PsxBusAccessReason::ok, value};
    }
    return psx_bus_read_u8(static_cast<const PsxBus&>(bus), address);
}

[[nodiscard]] inline PsxBusReadU16Result psx_bus_read_u16(const PsxBus& bus,
                                                           std::uint32_t address) noexcept {
    if ((address & 1u) != 0u) return {PsxBusAccessReason::misaligned, 0u};
    if (address == PsxBus::sio0_mode_address) return {PsxBusAccessReason::ok, bus.sio0.mode};
    if (address == PsxBus::sio0_control_address) return {PsxBusAccessReason::ok, bus.sio0.control};
    if (address == PsxBus::sio0_baud_address) return {PsxBusAccessReason::ok, bus.sio0.baud};
    if (address == PsxBus::interrupt_status_address) {
        return {PsxBusAccessReason::ok,
                static_cast<std::uint16_t>(bus.interrupt_status & PsxBus::interrupt_status_valid_bits)};
    }
    if (address == PsxBus::interrupt_mask_address) {
        return {PsxBusAccessReason::ok,
                static_cast<std::uint16_t>(bus.interrupt_mask & PsxBus::interrupt_mask_valid_bits)};
    }
    if (address == PsxBus::timer0_current_address) return {PsxBusAccessReason::ok, bus.timer0_current};
    if (address == PsxBus::timer0_mode_address) return {PsxBusAccessReason::ok, static_cast<std::uint16_t>(bus.timer0_mode & PsxBus::timer_mode_guest_bits)};
    if (address == PsxBus::timer0_target_address) return {PsxBusAccessReason::ok, bus.timer0_target};
    if (address == PsxBus::timer1_current_address) return {PsxBusAccessReason::ok, bus.timer1_current};
    if (address == PsxBus::timer1_mode_address) return {PsxBusAccessReason::ok, static_cast<std::uint16_t>(bus.timer1_mode & PsxBus::timer_mode_guest_bits)};
    if (address == PsxBus::timer1_target_address) return {PsxBusAccessReason::ok, bus.timer1_target};
    if (address == PsxBus::timer2_current_address) return {PsxBusAccessReason::ok, bus.timer2_current};
    if (address == PsxBus::timer2_mode_address) return {PsxBusAccessReason::ok, static_cast<std::uint16_t>(bus.timer2_mode & PsxBus::timer_mode_guest_bits)};
    if (address == PsxBus::timer2_target_address) return {PsxBusAccessReason::ok, bus.timer2_target};
    if (address >= PsxBus::spu_register_base &&
        address <= PsxBus::spu_register_end - 1u) {
        const auto index = static_cast<std::size_t>(
            (address - PsxBus::spu_register_base) / 2u);
        return {PsxBusAccessReason::ok, bus.spu_registers[index]};
    }
    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 2u, scratchpad_offset) == PsxBusAccessReason::ok) {
        const auto value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bus.scratchpad[scratchpad_offset]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bus.scratchpad[scratchpad_offset + 1u]) << 8u));
        return {PsxBusAccessReason::ok, value};
    }
    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset_u16(address, offset);
    if (reason != PsxBusAccessReason::ok) return {reason, 0u};
    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bus.ram[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bus.ram[offset + 1u]) << 8u));
    return {PsxBusAccessReason::ok, value};
}

[[nodiscard]] inline std::uint32_t psx_gpu_status_value(const PsxBus& bus) noexcept {
    constexpr std::uint32_t data_request = 1u << 25u;
    constexpr std::uint32_t ready_command = 1u << 26u;
    constexpr std::uint32_t ready_vram_to_cpu = 1u << 27u;
    constexpr std::uint32_t ready_dma_block = 1u << 28u;

    auto value = bus.gpu_status & ~data_request;
    const bool command_ready =
        bus.gpu_gp0_packet_count == 0u && !bus.gpu_polyline_active;
    if (command_ready) value |= ready_command;
    else value &= ~ready_command;

    bool dma_block_ready = true;
    if (psx_gpu_vram_to_cpu_active(bus) || bus.gpu_polyline_active) {
        dma_block_ready = false;
    } else if (bus.gpu_gp0_packet_count != 0u &&
               bus.gpu_gp0_packet_count < 0xfeu) {
        const auto group = bus.gpu_gp0_packet[0] >> 29u;
        if (group == 1u || group == 2u) dma_block_ready = false;
    }
    if (dma_block_ready) value |= ready_dma_block;
    else value &= ~ready_dma_block;

    bool request = false;
    switch ((value >> 29u) & 3u) {
    case 0u: request = false; break;
    case 1u: request = true; break;
    case 2u: request = dma_block_ready; break;
    default: request = (value & ready_vram_to_cpu) != 0u; break;
    }
    if (request) value |= data_request;
    return value;
}

[[nodiscard]] inline PsxBusReadU32Result psx_bus_read_u32(const PsxBus& bus,
                                                           std::uint32_t address) noexcept {
    if ((address & 3u) != 0u) return {PsxBusAccessReason::misaligned, 0u};
    if (address == PsxBus::spu_delay_address) {
        return {PsxBusAccessReason::ok, bus.spu_delay};
    }
    if (address == PsxBus::common_delay_address) {
        return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.common_delay)};
    }
    if (address == PsxBus::sio0_status_address) return {PsxBusAccessReason::ok, psx_sio0_status(bus.sio0)};
    if (address == PsxBus::interrupt_mask_address) {
        return {PsxBusAccessReason::ok,
                0xbf800000u |
                    static_cast<std::uint32_t>(bus.interrupt_mask & PsxBus::interrupt_mask_valid_bits)};
    }
    if (address == PsxBus::dma4_base_address) return {PsxBusAccessReason::ok, bus.dma4_base};
    if (address == PsxBus::dma4_block_control_address) return {PsxBusAccessReason::ok, bus.dma4_block_control};
    if (address == PsxBus::dma4_channel_control_address) return {PsxBusAccessReason::ok, bus.dma4_channel_control};
    if (address == PsxBus::dma2_base_address) return {PsxBusAccessReason::ok, bus.dma2_base};
    if (address == PsxBus::dma2_block_control_address) return {PsxBusAccessReason::ok, bus.dma2_block_control};
    if (address == PsxBus::dma2_channel_control_address) return {PsxBusAccessReason::ok, bus.dma2_channel_control};
    if (address == PsxBus::dma3_base_address) return {PsxBusAccessReason::ok, bus.dma3_base};
    if (address == PsxBus::dma3_block_control_address) return {PsxBusAccessReason::ok, bus.dma3_block_control};
    if (address == PsxBus::dma3_channel_control_address) return {PsxBusAccessReason::ok, bus.dma3_channel_control};
    if (address == PsxBus::dma6_base_address) return {PsxBusAccessReason::ok, bus.dma6_base};
    if (address == PsxBus::dma6_block_control_address) return {PsxBusAccessReason::ok, bus.dma6_block_control};
    if (address == PsxBus::dma6_channel_control_address) return {PsxBusAccessReason::ok, bus.dma6_channel_control};
    if (address == PsxBus::dma_control_address) return {PsxBusAccessReason::ok, bus.dma_control};
    if (address == PsxBus::dma_interrupt_address) return {PsxBusAccessReason::ok, psx_bus_dma_interrupt_value(bus)};
    if (address == PsxBus::timer0_current_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer0_current)};
    if (address == PsxBus::timer0_mode_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer0_mode & PsxBus::timer_mode_guest_bits)};
    if (address == PsxBus::timer0_target_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer0_target)};
    if (address == PsxBus::timer1_current_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer1_current)};
    if (address == PsxBus::timer1_mode_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer1_mode & PsxBus::timer_mode_guest_bits)};
    if (address == PsxBus::timer1_target_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer1_target)};
    if (address == PsxBus::timer2_current_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer2_current)};
    if (address == PsxBus::timer2_mode_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer2_mode & PsxBus::timer_mode_guest_bits)};
    if (address == PsxBus::timer2_target_address) return {PsxBusAccessReason::ok, static_cast<std::uint32_t>(bus.timer2_target)};
    if (address == PsxBus::gpu_gp0_address) return {PsxBusAccessReason::ok, bus.gpu_read_latch};
    if (address == PsxBus::gpu_gp1_address) {
        return {PsxBusAccessReason::ok, psx_gpu_status_value(bus)};
    }
    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 4u, scratchpad_offset) == PsxBusAccessReason::ok) {
        const auto value = static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset]) |
                           (static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 1u]) << 8u) |
                           (static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 2u]) << 16u) |
                           (static_cast<std::uint32_t>(bus.scratchpad[scratchpad_offset + 3u]) << 24u);
        return {PsxBusAccessReason::ok, value};
    }
    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset(address, offset);
    if (reason != PsxBusAccessReason::ok) return {reason, 0u};
    const auto value = static_cast<std::uint32_t>(bus.ram[offset]) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 1u]) << 8u) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 2u]) << 16u) |
                       (static_cast<std::uint32_t>(bus.ram[offset + 3u]) << 24u);
    return {PsxBusAccessReason::ok, value};
}

[[nodiscard]] inline PsxBusReadU32Result psx_bus_read_u32(PsxBus& bus,
                                                           std::uint32_t address) noexcept {
    if ((address & 3u) != 0u) return {PsxBusAccessReason::misaligned, 0u};
    if (address == PsxBus::gpu_gp0_address && psx_gpu_vram_to_cpu_active(bus)) {
        return {PsxBusAccessReason::ok, psx_gpu_read_vram_to_cpu_word(bus)};
    }
    return psx_bus_read_u32(static_cast<const PsxBus&>(bus), address);
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_write_u8(PsxBus& bus,
                                                          std::uint32_t address,
                                                          std::uint8_t value) noexcept {
    std::uint32_t physical = 0;
    if (!psx_bus_virtual_to_physical(address, physical)) return PsxBusAccessReason::unmapped;
    if (physical < PsxBus::default_ram_mirror_window) {
        const auto offset = static_cast<std::size_t>(physical & (PsxBus::main_ram_size - 1u));
        bus.ram[offset] = value;
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::sio0_data_address) {
        return psx_sio0_write_data(bus.sio0, value)
                   ? PsxBusAccessReason::ok
                   : PsxBusAccessReason::unmapped;
    }
    if (physical == PsxBus::cdrom_base_address) {
        bus.cdrom_index = static_cast<std::uint8_t>(value & PsxBus::cdrom_bank_mask);
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 1u && bus.cdrom_index == 0u) {
        return psx_bus_cdrom_execute_command(bus, value)
                   ? PsxBusAccessReason::ok
                   : PsxBusAccessReason::unmapped;
    }
    if (physical == PsxBus::cdrom_base_address + 2u && bus.cdrom_index == 0u) {
        if (bus.cdrom_parameter_count >= PsxBus::cdrom_parameter_capacity) return PsxBusAccessReason::unmapped;
        bus.cdrom_parameter_fifo[static_cast<std::size_t>(bus.cdrom_parameter_count)] = value;
        ++bus.cdrom_parameter_count;
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 2u && bus.cdrom_index == 1u) {
        const bool was_active = psx_bus_cdrom_irq_active(bus);
        bus.cdrom_interrupt_enable = static_cast<std::uint8_t>(value & PsxBus::cdrom_interrupt_bits);
        psx_bus_latch_cdrom_irq_rising_edge(bus, was_active);
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 3u && bus.cdrom_index == 1u) {
        if ((value & (PsxBus::cdrom_sound_map_clear | PsxBus::cdrom_decoder_reset)) != 0u) return PsxBusAccessReason::unmapped;
        bus.cdrom_interrupt_flags = static_cast<std::uint8_t>(bus.cdrom_interrupt_flags & static_cast<std::uint8_t>(~(value & PsxBus::cdrom_interrupt_bits)));
        if ((value & PsxBus::cdrom_clear_parameters) != 0u) psx_bus_cdrom_clear_parameters(bus);
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 3u && bus.cdrom_index == 0u) {
        if ((value & 0x7fu) != 0u) return PsxBusAccessReason::unmapped;
        bus.cdrom_host_control = value;
        if ((value & 0x80u) != 0u) {
            if (!bus.cdrom_sector_buffer_ready || bus.cdrom_data_count != 0u) return PsxBusAccessReason::unmapped;
            bus.cdrom_data_read_index = 0u;
            bus.cdrom_data_count = static_cast<std::uint16_t>(PsxBus::cdrom_data_sector_size);
            bus.cdrom_sector_buffer_ready = false;
        }
        return PsxBusAccessReason::ok;
    }
    if (physical == PsxBus::cdrom_base_address + 2u && bus.cdrom_index == 2u) { bus.cdrom_volume_pending_left_to_left = value; return PsxBusAccessReason::ok; }
    if (physical == PsxBus::cdrom_base_address + 3u && bus.cdrom_index == 2u) { bus.cdrom_volume_pending_left_to_right = value; return PsxBusAccessReason::ok; }
    if (physical == PsxBus::cdrom_base_address + 1u && bus.cdrom_index == 3u) { bus.cdrom_volume_pending_right_to_right = value; return PsxBusAccessReason::ok; }
    if (physical == PsxBus::cdrom_base_address + 2u && bus.cdrom_index == 3u) { bus.cdrom_volume_pending_right_to_left = value; return PsxBusAccessReason::ok; }
    if (physical == PsxBus::cdrom_base_address + 3u && bus.cdrom_index == 3u) {
        constexpr std::uint8_t apply_volume = 1u << 5u;
        if ((value & static_cast<std::uint8_t>(~apply_volume)) != 0u) return PsxBusAccessReason::unmapped;
        if ((value & apply_volume) != 0u) {
            bus.cdrom_volume_left_to_left = bus.cdrom_volume_pending_left_to_left;
            bus.cdrom_volume_left_to_right = bus.cdrom_volume_pending_left_to_right;
            bus.cdrom_volume_right_to_right = bus.cdrom_volume_pending_right_to_right;
            bus.cdrom_volume_right_to_left = bus.cdrom_volume_pending_right_to_left;
        }
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
    if (address == PsxBus::sio0_mode_address) { psx_sio0_write_mode(bus.sio0, value); return PsxBusAccessReason::ok; }
    if (address == PsxBus::sio0_control_address) { psx_sio0_write_control(bus.sio0, value); return PsxBusAccessReason::ok; }
    if (address == PsxBus::sio0_baud_address) { psx_sio0_write_baud(bus.sio0, value); return PsxBusAccessReason::ok; }
    if (address == PsxBus::interrupt_status_address) {
        const auto write_bits = static_cast<std::uint16_t>(value & PsxBus::interrupt_status_valid_bits);
        bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status & write_bits & PsxBus::interrupt_status_valid_bits);
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::interrupt_mask_address) { bus.interrupt_mask = static_cast<std::uint16_t>(value & PsxBus::interrupt_mask_valid_bits); return PsxBusAccessReason::ok; }
    if (address == PsxBus::timer0_current_address) { bus.timer0_current = value; bus.timer0_reset_pending = false; return PsxBusAccessReason::ok; }
    if (address == PsxBus::timer0_mode_address) {
        const auto next_epoch = static_cast<std::uint16_t>((bus.timer0_mode ^ PsxBus::timer_mode_write_epoch) & PsxBus::timer_mode_write_epoch);
        bus.timer0_mode = static_cast<std::uint16_t>((value & PsxBus::timer_mode_guest_bits) | next_epoch);
        bus.timer0_current = 0u;
        bus.timer0_reset_pending = false;
        bus.timer0_irq_fired = false;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer0_target_address) { bus.timer0_target = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::timer1_current_address) { bus.timer1_current = value; bus.timer1_reset_pending = false; return PsxBusAccessReason::ok; }
    if (address == PsxBus::timer1_mode_address) {
        const auto next_epoch = static_cast<std::uint16_t>((bus.timer1_mode ^ PsxBus::timer_mode_write_epoch) & PsxBus::timer_mode_write_epoch);
        bus.timer1_mode = static_cast<std::uint16_t>((value & PsxBus::timer_mode_guest_bits) | next_epoch);
        bus.timer1_current = 0u;
        bus.timer1_reset_pending = false;
        bus.timer1_irq_fired = false;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer1_target_address) { bus.timer1_target = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::timer2_current_address) { bus.timer2_current = value; bus.timer2_reset_pending = false; return PsxBusAccessReason::ok; }
    if (address == PsxBus::timer2_mode_address) {
        const auto next_epoch = static_cast<std::uint16_t>((bus.timer2_mode ^ PsxBus::timer_mode_write_epoch) & PsxBus::timer_mode_write_epoch);
        bus.timer2_mode = static_cast<std::uint16_t>((value & PsxBus::timer_mode_guest_bits) | next_epoch);
        bus.timer2_current = 0u;
        bus.timer2_reset_pending = false;
        bus.timer2_irq_fired = false;
        bus.timer2_clock_phase = 0u;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer2_target_address) { bus.timer2_target = value; return PsxBusAccessReason::ok; }
    if (address >= PsxBus::spu_register_base && address <= PsxBus::spu_register_end - 1u) {
        const auto index = static_cast<std::size_t>((address - PsxBus::spu_register_base) / 2u);
        bus.spu_registers[index] = value;
        return PsxBusAccessReason::ok;
    }
    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 2u, scratchpad_offset) == PsxBusAccessReason::ok) {
        bus.scratchpad[scratchpad_offset] = static_cast<std::uint8_t>(value);
        bus.scratchpad[scratchpad_offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        return PsxBusAccessReason::ok;
    }
    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset_u16(address, offset);
    if (reason != PsxBusAccessReason::ok) return reason;
    bus.ram[offset] = static_cast<std::uint8_t>(value);
    bus.ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusAccessReason psx_bus_write_u32(PsxBus& bus,
                                                           std::uint32_t address,
                                                           std::uint32_t value) noexcept {
    if ((address & 3u) != 0u) return PsxBusAccessReason::misaligned;
    if (address == PsxBus::spu_delay_address) { bus.spu_delay = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::common_delay_address) { bus.common_delay = static_cast<std::uint16_t>(value); return PsxBusAccessReason::ok; }
    if (address == PsxBus::interrupt_status_address) return psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
    if (address == PsxBus::interrupt_mask_address) return psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
    if (address == PsxBus::dma4_base_address) { bus.dma4_base = value & 0x00ffffffu; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma4_block_control_address) { bus.dma4_block_control = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma4_channel_control_address) {
        constexpr std::uint32_t supported_request_to_spu = 0x01000201u;
        if (value != supported_request_to_spu) return PsxBusAccessReason::unmapped;
        const auto block_words = bus.dma4_block_control & 0xffffu;
        const auto block_count = bus.dma4_block_control >> 16u;
        if (block_words == 0u || block_count == 0u) return PsxBusAccessReason::unmapped;
        const auto word_count = static_cast<std::uint64_t>(block_words) * block_count;
        const auto byte_count = word_count * 4u;
        if (byte_count > PsxBus::spu_ram_size) return PsxBusAccessReason::unmapped;
        const auto transfer_index = static_cast<std::size_t>((PsxBus::spu_transfer_address_register - PsxBus::spu_register_base) / 2u);
        auto source = static_cast<std::size_t>(bus.dma4_base & (PsxBus::main_ram_size - 1u));
        auto destination = static_cast<std::size_t>(bus.spu_registers[transfer_index]) * 8u;
        for (std::uint64_t i = 0u; i < byte_count; ++i) {
            bus.spu_ram[destination] = bus.ram[source];
            source = (source + 1u) & (PsxBus::main_ram_size - 1u);
            destination = (destination + 1u) & (PsxBus::spu_ram_size - 1u);
        }
        bus.dma4_base = static_cast<std::uint32_t>(source);
        bus.spu_registers[transfer_index] = static_cast<std::uint16_t>(destination / 8u);
        bus.dma4_channel_control = value & ~PsxBus::dma_channel_start_busy;
        const bool previous_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        bus.dma_interrupt |= 1u << 28u;
        const bool current_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        if (!previous_master && current_master) bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status | (1u << 3u));
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::dma2_base_address) { bus.dma2_base = value & 0x00ffffffu; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma2_block_control_address) { bus.dma2_block_control = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma2_channel_control_address) {
        constexpr std::uint32_t sync1_gpu_to_ram = 0x01000200u;
        constexpr std::uint32_t sync1_ram_to_gpu = 0x01000201u;
        constexpr std::uint32_t linked_list_ram_to_gpu = 0x01000401u;
        if (value == sync1_gpu_to_ram || value == sync1_ram_to_gpu) {
            const bool ram_to_gpu = value == sync1_ram_to_gpu;
            const auto gpu_dma_direction = (bus.gpu_status >> 29u) & 3u;
            if ((ram_to_gpu && gpu_dma_direction != 2u) ||
                (!ram_to_gpu && gpu_dma_direction != 3u)) {
                return PsxBusAccessReason::unmapped;
            }

            const auto raw_block_words = bus.dma2_block_control & 0xffffu;
            const auto raw_block_count = bus.dma2_block_control >> 16u;
            const auto block_words = raw_block_words == 0u ? 0x10000u : raw_block_words;
            const auto block_count = raw_block_count == 0u ? 0x10000u : raw_block_count;
            const auto word_count =
                static_cast<std::uint64_t>(block_words) * block_count;
            if (word_count == 0u || word_count > PsxBus::main_ram_size / 4u) {
                return PsxBusAccessReason::unmapped;
            }

            if (!ram_to_gpu) {
                if (!psx_gpu_vram_to_cpu_active(bus)) return PsxBusAccessReason::unmapped;
                const auto dimensions = bus.gpu_gp0_packet[1];
                const auto total_pixels =
                    (dimensions & 0xffffu) * (dimensions >> 16u);
                const auto pixels_read = bus.gpu_gp0_packet[2];
                if (pixels_read > total_pixels) return PsxBusAccessReason::unmapped;
                const auto remaining_words =
                    (static_cast<std::uint64_t>(total_pixels - pixels_read) + 1u) / 2u;
                if (word_count > remaining_words) return PsxBusAccessReason::unmapped;
            }

            auto memory_address = bus.dma2_base & 0x00fffffcu;
            for (std::uint64_t word = 0u; word < word_count; ++word) {
                const auto offset = static_cast<std::size_t>(
                    memory_address & (PsxBus::main_ram_size - 1u));
                if (ram_to_gpu) {
                    const auto gp0 = static_cast<std::uint32_t>(bus.ram[offset + 0u]) |
                                     (static_cast<std::uint32_t>(bus.ram[offset + 1u]) << 8u) |
                                     (static_cast<std::uint32_t>(bus.ram[offset + 2u]) << 16u) |
                                     (static_cast<std::uint32_t>(bus.ram[offset + 3u]) << 24u);
                    psx_gpu_write_gp0(bus, gp0);
                } else {
                    const auto gpu_word = psx_bus_read_u32(bus, PsxBus::gpu_gp0_address);
                    if (gpu_word.reason != PsxBusAccessReason::ok) return gpu_word.reason;
                    bus.ram[offset + 0u] = static_cast<std::uint8_t>(gpu_word.value);
                    bus.ram[offset + 1u] = static_cast<std::uint8_t>(gpu_word.value >> 8u);
                    bus.ram[offset + 2u] = static_cast<std::uint8_t>(gpu_word.value >> 16u);
                    bus.ram[offset + 3u] = static_cast<std::uint8_t>(gpu_word.value >> 24u);
                }
                memory_address = (memory_address + 4u) & 0x00ffffffu;
            }

            bus.dma2_base = memory_address;
            bus.dma2_block_control = raw_block_words;
            bus.dma2_channel_control = value & ~PsxBus::dma_channel_start_busy;
            const bool previous_master =
                (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
            bus.dma_interrupt |= 1u << 26u;
            const bool current_master =
                (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
            if (!previous_master && current_master) {
                bus.interrupt_status = static_cast<std::uint16_t>(
                    bus.interrupt_status | (1u << 3u));
            }
            return PsxBusAccessReason::ok;
        }
        if (value == linked_list_ram_to_gpu) {
            auto packet_address = bus.dma2_base & 0x00fffffcu;
            constexpr std::size_t maximum_packets = PsxBus::main_ram_size / sizeof(std::uint32_t);
            bool terminated = false;
            for (std::size_t packet = 0u; packet < maximum_packets; ++packet) {
                const auto header_offset = static_cast<std::size_t>(packet_address & (PsxBus::main_ram_size - 1u));
                const auto header = static_cast<std::uint32_t>(bus.ram[header_offset]) |
                                    (static_cast<std::uint32_t>(bus.ram[header_offset + 1u]) << 8u) |
                                    (static_cast<std::uint32_t>(bus.ram[header_offset + 2u]) << 16u) |
                                    (static_cast<std::uint32_t>(bus.ram[header_offset + 3u]) << 24u);
                const auto word_count = header >> 24u;
                auto word_address = packet_address + 4u;
                for (std::uint32_t word = 0u; word < word_count; ++word) {
                    const auto word_offset = static_cast<std::size_t>(word_address & (PsxBus::main_ram_size - 1u));
                    const auto gp0 = static_cast<std::uint32_t>(bus.ram[word_offset]) |
                                     (static_cast<std::uint32_t>(bus.ram[word_offset + 1u]) << 8u) |
                                     (static_cast<std::uint32_t>(bus.ram[word_offset + 2u]) << 16u) |
                                     (static_cast<std::uint32_t>(bus.ram[word_offset + 3u]) << 24u);
                    psx_gpu_write_gp0(bus, gp0);
                    word_address += 4u;
                }
                const auto next = header & 0x00ffffffu;
                bus.dma2_base = next;
                if (next == 0x00ffffffu) { terminated = true; break; }
                packet_address = next & 0x00fffffcu;
            }
            if (!terminated) return PsxBusAccessReason::unmapped;
            bus.dma2_channel_control = value & ~PsxBus::dma_channel_start_busy;
            const bool previous_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
            bus.dma_interrupt |= 1u << 26u;
            const bool current_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
            if (!previous_master && current_master) bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status | (1u << 3u));
            return PsxBusAccessReason::ok;
        }
        if ((value & PsxBus::dma_channel_start_busy) != 0u) return PsxBusAccessReason::unmapped;
        bus.dma2_channel_control = value & PsxBus::dma2_channel_control_mask;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::dma3_base_address) { bus.dma3_base = value & 0x00ffffffu; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma3_block_control_address) { bus.dma3_block_control = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma3_channel_control_address) {
        constexpr std::uint32_t cdrom_burst_to_ram = 0x11000000u;
        if ((value & PsxBus::dma_channel_start_busy) == 0u) { bus.dma3_channel_control = value; return PsxBusAccessReason::ok; }
        if (value != cdrom_burst_to_ram) return PsxBusAccessReason::unmapped;
        const auto encoded_words = bus.dma3_block_control & 0xffffu;
        const auto word_count = encoded_words == 0u ? 0x10000u : encoded_words;
        const auto byte_count = static_cast<std::uint64_t>(word_count) * 4u;
        if (byte_count > bus.cdrom_data_count) return PsxBusAccessReason::unmapped;
        auto destination = static_cast<std::size_t>(bus.dma3_base & (PsxBus::main_ram_size - 1u));
        for (std::uint64_t i = 0u; i < byte_count; ++i) {
            const auto data = psx_bus_read_u8(bus, PsxBus::cdrom_base_address + 2u);
            if (data.reason != PsxBusAccessReason::ok) return data.reason;
            bus.ram[destination] = data.value;
            destination = (destination + 1u) & (PsxBus::main_ram_size - 1u);
        }
        bus.dma3_channel_control = value & ~(PsxBus::dma_channel_start_busy | (1u << 28u));
        const bool previous_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        bus.dma_interrupt |= 1u << 27u;
        const bool current_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        if (!previous_master && current_master) bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status | (1u << 3u));
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::dma6_base_address) { bus.dma6_base = value & 0x00ffffffu; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma6_block_control_address) { bus.dma6_block_control = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma6_channel_control_address) {
        constexpr std::uint32_t otc_manual_start = 0x11000002u;
        if (value == otc_manual_start) {
            const auto word_count = bus.dma6_block_control & 0xffffu;
            if (word_count == 0u) return PsxBusAccessReason::unmapped;
            auto current = bus.dma6_base & 0x00fffffcu;
            for (std::uint32_t word = 0u; word < word_count; ++word) {
                const auto link = word + 1u == word_count ? 0x00ffffffu : ((current - 4u) & 0x00ffffffu);
                const auto offset = static_cast<std::size_t>(current & (PsxBus::main_ram_size - 1u));
                bus.ram[offset + 0u] = static_cast<std::uint8_t>(link);
                bus.ram[offset + 1u] = static_cast<std::uint8_t>(link >> 8u);
                bus.ram[offset + 2u] = static_cast<std::uint8_t>(link >> 16u);
                bus.ram[offset + 3u] = 0u;
                current = (current - 4u) & 0x00ffffffu;
            }
            bus.dma6_base = current;
            bus.dma6_block_control = 0u;
            bus.dma6_channel_control = value & ~(PsxBus::dma_channel_start_busy | (1u << 28u));
            const bool previous_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
            bus.dma_interrupt |= 1u << 30u;
            const bool current_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
            if (!previous_master && current_master) bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status | (1u << 3u));
            return PsxBusAccessReason::ok;
        }
        if ((value & PsxBus::dma_channel_start_busy) != 0u) return PsxBusAccessReason::unmapped;
        bus.dma6_channel_control = value;
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::dma_control_address) { bus.dma_control = value; return PsxBusAccessReason::ok; }
    if (address == PsxBus::dma_interrupt_address) {
        const bool previous_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        const auto previous_flags = bus.dma_interrupt & PsxBus::dma_interrupt_flag_mask;
        const auto acknowledged = value & PsxBus::dma_interrupt_flag_mask;
        const auto remaining_flags = previous_flags & ~acknowledged;
        const auto control = value & PsxBus::dma_interrupt_control_mask;
        bus.dma_interrupt = control | remaining_flags;
        const bool current_master = (psx_bus_dma_interrupt_value(bus) & PsxBus::dma_interrupt_master_flag) != 0u;
        if (!previous_master && current_master) bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status | (1u << 3u));
        return PsxBusAccessReason::ok;
    }
    if (address == PsxBus::timer0_current_address || address == PsxBus::timer0_mode_address || address == PsxBus::timer0_target_address ||
        address == PsxBus::timer1_current_address || address == PsxBus::timer1_mode_address || address == PsxBus::timer1_target_address ||
        address == PsxBus::timer2_current_address || address == PsxBus::timer2_mode_address || address == PsxBus::timer2_target_address) {
        return psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
    }
    if (address == PsxBus::gpu_gp0_address) { psx_gpu_write_gp0(bus, value); return PsxBusAccessReason::ok; }
    if (address >= PsxBus::spu_register_base && address <= PsxBus::spu_register_end - 3u) {
        const auto low = psx_bus_write_u16(bus, address, static_cast<std::uint16_t>(value));
        if (low != PsxBusAccessReason::ok) return low;
        return psx_bus_write_u16(bus, address + 2u, static_cast<std::uint16_t>(value >> 16u));
    }
    if (address == PsxBus::gpu_gp1_address) {
        const auto command = static_cast<std::uint8_t>(value >> 24u);
        if (command == 0x00u) {
            bus.gpu_status = PsxBus::gpu_status_reset;
            bus.gpu_read_latch = 0u;
            bus.gpu_draw_mode = 0u;
            bus.gpu_texture_window = 0u;
            bus.gpu_drawing_area_top_left = 0u;
            bus.gpu_drawing_area_bottom_right = 0u;
            bus.gpu_drawing_offset = 0u;
            bus.gpu_display_vram_start = 0u;
            bus.gpu_horizontal_display_range = PsxBus::gpu_horizontal_display_range_reset;
            bus.gpu_vertical_display_range = PsxBus::gpu_vertical_display_range_reset;
            bus.gpu_display_mode = 0u;
            bus.gpu_gp0_packet = {};
            bus.gpu_gp0_packet_count = 0u;
            bus.gpu_gp0_packet_words = 0u;
            bus.gpu_polyline_active = false;
            bus.gpu_polyline_gouraud = false;
            bus.gpu_polyline_expect_color = false;
            return PsxBusAccessReason::ok;
        }
        if (command == 0x01u) {
            bus.gpu_gp0_packet = {};
            bus.gpu_gp0_packet_count = 0u;
            bus.gpu_gp0_packet_words = 0u;
            bus.gpu_polyline_active = false;
            bus.gpu_polyline_gouraud = false;
            bus.gpu_polyline_expect_color = false;
            bus.gpu_status &= ~(1u << 27u);
            return PsxBusAccessReason::ok;
        }
        if (command == 0x02u) {
            bus.gpu_status &= ~(1u << 24u);
            return PsxBusAccessReason::ok;
        }
        if (command == 0x05u) {
            bus.gpu_display_vram_start = value & 0x0007ffffu;
            return PsxBusAccessReason::ok;
        }
        if (command == 0x06u) {
            bus.gpu_horizontal_display_range = value & 0x00ffffffu;
            return PsxBusAccessReason::ok;
        }
        if (command == 0x07u) {
            bus.gpu_vertical_display_range = value & 0x000fffffu;
            return PsxBusAccessReason::ok;
        }
        if (command == 0x08u) {
            const auto mode = static_cast<std::uint8_t>(value & 0xffu);
            bus.gpu_display_mode = mode;
            constexpr std::uint32_t display_status_mask = 0x007f0000u;
            const auto display_status =
                (static_cast<std::uint32_t>(mode & 0x03u) << 17u) |
                (static_cast<std::uint32_t>(mode & 0x3cu) << 17u) |
                (static_cast<std::uint32_t>(mode & 0x40u) << 10u);
            bus.gpu_status = (bus.gpu_status & ~display_status_mask) | display_status;
            return PsxBusAccessReason::ok;
        }
        if (command == 0x10u) {
            const auto index = value & 0x00ffffffu;
            switch (index) {
            case 0x02u: bus.gpu_read_latch = bus.gpu_texture_window; return PsxBusAccessReason::ok;
            case 0x03u: bus.gpu_read_latch = bus.gpu_drawing_area_top_left; return PsxBusAccessReason::ok;
            case 0x04u: bus.gpu_read_latch = bus.gpu_drawing_area_bottom_right; return PsxBusAccessReason::ok;
            case 0x05u: bus.gpu_read_latch = bus.gpu_drawing_offset; return PsxBusAccessReason::ok;
            case 0x07u: bus.gpu_read_latch = 2u; return PsxBusAccessReason::ok;
            default: return PsxBusAccessReason::unmapped;
            }
        }
        if (command == 0x04u) {
            constexpr std::uint32_t dma_direction_mask = 3u << 29u;
            const auto direction = value & 3u;
            bus.gpu_status = (bus.gpu_status & ~dma_direction_mask) | (direction << 29u);
            return PsxBusAccessReason::ok;
        }
        if (command != 0x03u) return PsxBusAccessReason::unmapped;
        if ((value & 1u) != 0u) bus.gpu_status |= PsxBus::gpu_status_display_disabled;
        else bus.gpu_status &= ~PsxBus::gpu_status_display_disabled;
        return PsxBusAccessReason::ok;
    }
    std::size_t scratchpad_offset = 0;
    if (psx_bus_scratchpad_offset(address, 4u, scratchpad_offset) == PsxBusAccessReason::ok) {
        bus.scratchpad[scratchpad_offset] = static_cast<std::uint8_t>(value);
        bus.scratchpad[scratchpad_offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        bus.scratchpad[scratchpad_offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
        bus.scratchpad[scratchpad_offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
        return PsxBusAccessReason::ok;
    }
    std::size_t offset = 0;
    const auto reason = psx_bus_ram_offset(address, offset);
    if (reason != PsxBusAccessReason::ok) return reason;
    bus.ram[offset] = static_cast<std::uint8_t>(value);
    bus.ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bus.ram[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bus.ram[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    return PsxBusAccessReason::ok;
}

inline void psx_bus_tick_root_counter(PsxBus& bus,
                                              std::uint16_t& current,
                                              std::uint16_t mode,
                                              std::uint16_t target,
                                              bool& reset_pending,
                                              bool& irq_fired,
                                              std::uint16_t irq_bit,
                                              std::uint32_t ticks) noexcept {
    constexpr std::uint16_t reset_at_target = 1u << 3u;
    constexpr std::uint16_t irq_at_target = 1u << 4u;
    constexpr std::uint16_t irq_at_ffff = 1u << 5u;
    constexpr std::uint16_t repeat_irq = 1u << 6u;
    for (std::uint32_t tick = 0u; tick < ticks; ++tick) {
        if (reset_pending) {
            current = 0u;
            reset_pending = false;
            continue;
        }
        current = static_cast<std::uint16_t>(current + 1u);
        const bool hit_target = current == target;
        const bool hit_ffff = current == 0xffffu;
        const bool irq_enabled =
            (hit_target && (mode & irq_at_target) != 0u) ||
            (hit_ffff && (mode & irq_at_ffff) != 0u);
        if (irq_enabled && (((mode & repeat_irq) != 0u) || !irq_fired)) {
            bus.interrupt_status = static_cast<std::uint16_t>(
                bus.interrupt_status | irq_bit);
            if ((mode & repeat_irq) == 0u) irq_fired = true;
        }
        if (((mode & reset_at_target) != 0u && hit_target) ||
            ((mode & reset_at_target) == 0u && hit_ffff)) {
            reset_pending = true;
        }
    }
}
inline void psx_bus_tick(PsxBus& bus, std::uint32_t cpu_cycles) noexcept {
    constexpr std::uint16_t synchronization_enable = 1u << 0u;
    constexpr std::uint16_t timer1_hblank_clock = 1u << 8u;
    constexpr std::uint16_t timer0_interrupt = 1u << 4u;
    constexpr std::uint16_t timer1_interrupt = 1u << 5u;
    constexpr std::uint16_t timer2_interrupt = 1u << 6u;
    if ((bus.timer0_mode & synchronization_enable) == 0u) {
        const auto source = static_cast<std::uint16_t>((bus.timer0_mode >> 8u) & 3u);
        if (source == 0u || source == 2u) {
            psx_bus_tick_root_counter(bus, bus.timer0_current, bus.timer0_mode,
                                      bus.timer0_target, bus.timer0_reset_pending,
                                      bus.timer0_irq_fired, timer0_interrupt, cpu_cycles);
        }
    }
    if ((bus.timer1_mode & synchronization_enable) == 0u) {
        const auto source = static_cast<std::uint16_t>((bus.timer1_mode >> 8u) & 3u);
        if (source == 0u || source == 2u) {
            psx_bus_tick_root_counter(bus, bus.timer1_current, bus.timer1_mode,
                                      bus.timer1_target, bus.timer1_reset_pending,
                                      bus.timer1_irq_fired, timer1_interrupt, cpu_cycles);
        }
    }
    if ((bus.timer2_mode & synchronization_enable) == 0u) {
        const auto source = static_cast<std::uint16_t>((bus.timer2_mode >> 8u) & 3u);
        if (source == 0u || source == 1u) {
            psx_bus_tick_root_counter(bus, bus.timer2_current, bus.timer2_mode,
                                      bus.timer2_target, bus.timer2_reset_pending,
                                      bus.timer2_irq_fired, timer2_interrupt, cpu_cycles);
        } else {
            const auto total = static_cast<std::uint64_t>(bus.timer2_clock_phase) + cpu_cycles;
            const auto counter_ticks = static_cast<std::uint32_t>(total / 8u);
            bus.timer2_clock_phase = static_cast<std::uint8_t>(total % 8u);
            psx_bus_tick_root_counter(bus, bus.timer2_current, bus.timer2_mode,
                                      bus.timer2_target, bus.timer2_reset_pending,
                                      bus.timer2_irq_fired, timer2_interrupt, counter_ticks);
        }
    }
    constexpr std::uint16_t vblank_interrupt = 1u << 0u;
    constexpr std::uint16_t ntsc_vblank_start_line = 240u;
    constexpr std::uint16_t ntsc_scanlines_per_frame = 263u;
    constexpr std::uint64_t cpu_clock_hz = 33'868'800u;
    constexpr std::uint64_t video_clock_hz = 53'693'175u;
    constexpr std::uint64_t video_clocks_per_scanline = 3'413u;
    constexpr std::uint64_t phase_per_hblank = cpu_clock_hz * video_clocks_per_scanline;
    bus.video_clock_phase += static_cast<std::uint64_t>(cpu_cycles) * video_clock_hz;
    while (bus.video_clock_phase >= phase_per_hblank) {
        bus.video_clock_phase -= phase_per_hblank;
        if ((bus.timer1_mode & timer1_hblank_clock) != 0u) {
            psx_bus_tick_root_counter(bus, bus.timer1_current, bus.timer1_mode,
                                      bus.timer1_target, bus.timer1_reset_pending,
                                      bus.timer1_irq_fired, timer1_interrupt, 1u);
        }
        bus.gpu_scanline = static_cast<std::uint16_t>(bus.gpu_scanline + 1u);
        if (bus.gpu_scanline == ntsc_vblank_start_line) bus.interrupt_status = static_cast<std::uint16_t>(bus.interrupt_status | vblank_interrupt);
        else if (bus.gpu_scanline == ntsc_scanlines_per_frame) bus.gpu_scanline = 0u;
    }
    if (bus.cdrom_async_cycles != 0u) {
        if (cpu_cycles < bus.cdrom_async_cycles) bus.cdrom_async_cycles -= cpu_cycles;
        else if ((bus.cdrom_interrupt_flags & PsxBus::cdrom_hc05_interrupt_bits) == 0u && bus.cdrom_result_count == 0u) {
            const bool was_active = psx_bus_cdrom_irq_active(bus);
            bus.cdrom_async_cycles = 0u;
            (void)psx_bus_cdrom_push_result(bus, bus.cdrom_status);
            bus.cdrom_interrupt_flags = bus.cdrom_async_interrupt;
            bus.cdrom_async_interrupt = 0u;
            psx_bus_latch_cdrom_irq_rising_edge(bus, was_active);
        }
    }
}

template <typename RuntimeLike>
[[nodiscard]] inline Result<void> mount_psx_cdrom_image(RuntimeLike& runtime,
                                                         const std::filesystem::path& image_path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(image_path, ec);
    if (ec || size == 0u || (size % PsxBus::cdrom_data_sector_size) != 0u) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "prepared PS1 disc must be a non-empty 2048-byte-sector image");
    }
    auto& bus = runtime.bus;
    bus.cdrom_image_path = image_path;
    bus.cdrom_image_sector_count = static_cast<std::uint64_t>(size / PsxBus::cdrom_data_sector_size);
    bus.cdrom_image_mounted = true;
    bus.cdrom_reading = false;
    bus.cdrom_sector_buffer_ready = false;
    bus.cdrom_data_read_index = 0u;
    bus.cdrom_data_count = 0u;
    return Result<void>::success();
}

template <typename RuntimeLike>
inline void service_psx_cdrom(RuntimeLike& runtime) noexcept {
    auto& bus = runtime.bus;
    if (!bus.cdrom_image_mounted || !bus.cdrom_reading ||
        (bus.cdrom_interrupt_flags & PsxBus::cdrom_hc05_interrupt_bits) != 0u ||
        bus.cdrom_result_count != 0u || bus.cdrom_sector_buffer_ready || bus.cdrom_data_count != 0u) return;
    if (bus.cdrom_location_lba >= bus.cdrom_image_sector_count) {
        bus.cdrom_reading = false;
        (void)psx_bus_cdrom_raise_interrupt(bus, PsxBus::cdrom_interrupt_data_end, {bus.cdrom_status});
        return;
    }
    try {
        std::ifstream in(bus.cdrom_image_path, std::ios::binary);
        if (!in) {
            bus.cdrom_reading = false;
            (void)psx_bus_cdrom_raise_interrupt(bus, PsxBus::cdrom_interrupt_error,
                                                {static_cast<std::uint8_t>(bus.cdrom_status | 0x01u)});
            return;
        }
        const auto offset = static_cast<std::uint64_t>(bus.cdrom_location_lba) * PsxBus::cdrom_data_sector_size;
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        in.read(reinterpret_cast<char*>(bus.cdrom_sector_buffer.data()),
                static_cast<std::streamsize>(PsxBus::cdrom_data_sector_size));
        if (!in || in.gcount() != static_cast<std::streamsize>(PsxBus::cdrom_data_sector_size)) {
            bus.cdrom_reading = false;
            (void)psx_bus_cdrom_raise_interrupt(bus, PsxBus::cdrom_interrupt_error,
                                                {static_cast<std::uint8_t>(bus.cdrom_status | 0x01u)});
            return;
        }
    } catch (...) {
        bus.cdrom_reading = false;
        (void)psx_bus_cdrom_raise_interrupt(bus, PsxBus::cdrom_interrupt_error,
                                            {static_cast<std::uint8_t>(bus.cdrom_status | 0x01u)});
        return;
    }
    bus.cdrom_sector_buffer_ready = true;
    ++bus.cdrom_location_lba;
    (void)psx_bus_cdrom_raise_interrupt(bus, PsxBus::cdrom_interrupt_data_ready, {bus.cdrom_status});
}

} // namespace jojo
