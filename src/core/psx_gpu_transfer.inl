namespace psx_gpu_transfer_detail {

inline constexpr std::uint8_t vram_to_cpu_state = 0xfeu;

[[nodiscard]] inline std::uint32_t normalized_copy_width(std::uint32_t raw) noexcept {
    return ((raw - 1u) & 0x3ffu) + 1u;
}

[[nodiscard]] inline std::uint32_t normalized_copy_height(std::uint32_t raw) noexcept {
    return ((raw - 1u) & 0x1ffu) + 1u;
}

[[nodiscard]] inline std::size_t vram_index(std::uint32_t x,
                                             std::uint32_t y) noexcept {
    return static_cast<std::size_t>(y & 0x1ffu) * PsxBus::gpu_vram_width +
           static_cast<std::size_t>(x & 0x3ffu);
}

} // namespace psx_gpu_transfer_detail

[[nodiscard]] inline bool psx_gpu_vram_to_cpu_active(const PsxBus& bus) noexcept {
    return bus.gpu_gp0_packet_count == psx_gpu_transfer_detail::vram_to_cpu_state;
}

inline void psx_gpu_execute_vram_to_vram(PsxBus& bus) noexcept {
    const auto source = bus.gpu_gp0_packet[1];
    const auto destination = bus.gpu_gp0_packet[2];
    const auto size = bus.gpu_gp0_packet[3];
    const auto source_x = source & 0x3ffu;
    const auto source_y = (source >> 16u) & 0x1ffu;
    const auto destination_x = destination & 0x3ffu;
    const auto destination_y = (destination >> 16u) & 0x1ffu;
    const auto width = psx_gpu_transfer_detail::normalized_copy_width(size & 0xffffu);
    const auto height = psx_gpu_transfer_detail::normalized_copy_height(size >> 16u);
    const bool force_mask = (bus.gpu_status & (1u << 11u)) != 0u;
    const bool check_mask = (bus.gpu_status & (1u << 12u)) != 0u;

    for (std::uint32_t row = 0u; row < height; ++row) {
        for (std::uint32_t column = 0u; column < width; ++column) {
            const auto source_index = psx_gpu_transfer_detail::vram_index(
                source_x + column, source_y + row);
            const auto destination_index = psx_gpu_transfer_detail::vram_index(
                destination_x + column, destination_y + row);
            auto pixel = bus.gpu_vram[source_index];
            auto& old_pixel = bus.gpu_vram[destination_index];
            if (check_mask && (old_pixel & 0x8000u) != 0u) continue;
            if (force_mask) pixel = static_cast<std::uint16_t>(pixel | 0x8000u);
            old_pixel = pixel;
        }
    }
}

inline void psx_gpu_begin_vram_to_cpu(PsxBus& bus) noexcept {
    const auto source = bus.gpu_gp0_packet[1];
    const auto size = bus.gpu_gp0_packet[2];
    const auto width = psx_gpu_transfer_detail::normalized_copy_width(size & 0xffffu);
    const auto height = psx_gpu_transfer_detail::normalized_copy_height(size >> 16u);
    bus.gpu_gp0_packet[0] = source;
    bus.gpu_gp0_packet[1] = width | (height << 16u);
    bus.gpu_gp0_packet[2] = 0u;
    bus.gpu_gp0_packet_count = psx_gpu_transfer_detail::vram_to_cpu_state;
    bus.gpu_gp0_packet_words = 0u;
    bus.gpu_status |= 1u << 27u;
}

[[nodiscard]] inline std::uint32_t psx_gpu_read_vram_to_cpu_word(PsxBus& bus) noexcept {
    const auto source = bus.gpu_gp0_packet[0];
    const auto dimensions = bus.gpu_gp0_packet[1];
    auto read = bus.gpu_gp0_packet[2];
    const auto source_x = source & 0x3ffu;
    const auto source_y = (source >> 16u) & 0x1ffu;
    const auto width = dimensions & 0xffffu;
    const auto height = dimensions >> 16u;
    const auto total = width * height;
    std::uint32_t word = 0u;

    for (std::uint32_t half = 0u; half < 2u && read < total; ++half) {
        const auto local_x = read % width;
        const auto local_y = read / width;
        const auto pixel = bus.gpu_vram[psx_gpu_transfer_detail::vram_index(
            source_x + local_x, source_y + local_y)];
        word |= static_cast<std::uint32_t>(pixel) << (half * 16u);
        ++read;
    }

    bus.gpu_read_latch = word;
    bus.gpu_gp0_packet[2] = read;
    if (read >= total) {
        bus.gpu_gp0_packet = {};
        bus.gpu_gp0_packet_count = 0u;
        bus.gpu_gp0_packet_words = 0u;
        bus.gpu_status &= ~(1u << 27u);
    }
    return word;
}
