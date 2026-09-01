namespace psx_gpu_texture_detail {

[[nodiscard]] inline std::uint8_t apply_texture_window_axis(
    std::uint8_t coordinate,
    std::uint32_t mask,
    std::uint32_t offset) noexcept {
    const auto clear_mask = static_cast<std::uint8_t>((mask & 0x1fu) << 3u);
    const auto replacement = static_cast<std::uint8_t>((offset & mask & 0x1fu) << 3u);
    return static_cast<std::uint8_t>(
        (coordinate & static_cast<std::uint8_t>(~clear_mask)) | replacement);
}

struct TextureCoordinate {
    std::uint8_t u{};
    std::uint8_t v{};
};

[[nodiscard]] inline TextureCoordinate apply_texture_window(
    const PsxBus& bus,
    std::uint8_t u,
    std::uint8_t v) noexcept {
    const auto window = bus.gpu_texture_window;
    const auto mask_x = window & 0x1fu;
    const auto mask_y = (window >> 5u) & 0x1fu;
    const auto offset_x = (window >> 10u) & 0x1fu;
    const auto offset_y = (window >> 15u) & 0x1fu;
    return {
        apply_texture_window_axis(u, mask_x, offset_x),
        apply_texture_window_axis(v, mask_y, offset_y),
    };
}

[[nodiscard]] inline std::uint16_t sample_texture(
    const PsxBus& bus,
    std::uint8_t u,
    std::uint8_t v,
    std::uint16_t clut) noexcept {
    const auto adjusted = apply_texture_window(bus, u, v);
    const auto page_x = static_cast<std::uint32_t>(bus.gpu_draw_mode & 0x0fu) * 64u;
    const auto page_y = static_cast<std::uint32_t>((bus.gpu_draw_mode >> 4u) & 1u) * 256u;
    const auto depth = (bus.gpu_draw_mode >> 7u) & 3u;
    const auto row = (page_y + adjusted.v) & 0x1ffu;

    if (depth == 0u) {
        const auto word_x =
            (page_x + static_cast<std::uint32_t>(adjusted.u) / 4u) & 0x3ffu;
        const auto packed = bus.gpu_vram[
            static_cast<std::size_t>(row) * PsxBus::gpu_vram_width + word_x];
        const auto shift = static_cast<std::uint32_t>(adjusted.u & 3u) * 4u;
        const auto palette_index =
            static_cast<std::uint32_t>((packed >> shift) & 0x0fu);
        const auto clut_x = static_cast<std::uint32_t>(clut & 0x3fu) * 16u;
        const auto clut_y = static_cast<std::uint32_t>((clut >> 6u) & 0x1ffu);
        return bus.gpu_vram[
            static_cast<std::size_t>(clut_y) * PsxBus::gpu_vram_width +
            ((clut_x + palette_index) & 0x3ffu)];
    }

    if (depth == 1u) {
        const auto word_x =
            (page_x + static_cast<std::uint32_t>(adjusted.u) / 2u) & 0x3ffu;
        const auto packed = bus.gpu_vram[
            static_cast<std::size_t>(row) * PsxBus::gpu_vram_width + word_x];
        const auto shift = static_cast<std::uint32_t>(adjusted.u & 1u) * 8u;
        const auto palette_index =
            static_cast<std::uint32_t>((packed >> shift) & 0xffu);
        const auto clut_x = static_cast<std::uint32_t>(clut & 0x3fu) * 16u;
        const auto clut_y = static_cast<std::uint32_t>((clut >> 6u) & 0x1ffu);
        return bus.gpu_vram[
            static_cast<std::size_t>(clut_y) * PsxBus::gpu_vram_width +
            ((clut_x + palette_index) & 0x3ffu)];
    }

    const auto word_x =
        (page_x + static_cast<std::uint32_t>(adjusted.u)) & 0x3ffu;
    return bus.gpu_vram[
        static_cast<std::size_t>(row) * PsxBus::gpu_vram_width + word_x];
}

[[nodiscard]] inline std::uint16_t modulate_texture(
    std::uint16_t texel,
    std::uint32_t command) noexcept {
    if ((command & (1u << 24u)) != 0u) return texel;

    const auto modulate_channel = [](std::uint32_t channel,
                                     std::uint32_t brightness) noexcept {
        return std::min<std::uint32_t>(31u, (channel * brightness) >> 7u);
    };

    const auto red = modulate_channel(texel & 0x1fu, command & 0xffu);
    const auto green = modulate_channel((texel >> 5u) & 0x1fu,
                                        (command >> 8u) & 0xffu);
    const auto blue = modulate_channel((texel >> 10u) & 0x1fu,
                                       (command >> 16u) & 0xffu);
    return static_cast<std::uint16_t>(
        (texel & 0x8000u) | red | (green << 5u) | (blue << 10u));
}

} // namespace psx_gpu_texture_detail

inline void psx_gpu_execute_textured_rectangle(PsxBus& bus) noexcept {
    const auto command = bus.gpu_gp0_packet[0];
    const auto position = bus.gpu_gp0_packet[1];
    const auto uv = bus.gpu_gp0_packet[2];
    const auto size_mode = (command >> 27u) & 3u;

    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    switch (size_mode) {
    case 0u:
        width = bus.gpu_gp0_packet[3] & 0xffffu;
        height = bus.gpu_gp0_packet[3] >> 16u;
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
    const auto base_u = static_cast<std::uint8_t>(uv);
    const auto base_v = static_cast<std::uint8_t>(uv >> 8u);
    const auto clut = static_cast<std::uint16_t>(uv >> 16u);
    const bool flip_x = (bus.gpu_draw_mode & (1u << 12u)) != 0u;
    const bool flip_y = (bus.gpu_draw_mode & (1u << 13u)) != 0u;

    for (std::uint32_t row = 0u; row < height; ++row) {
        for (std::uint32_t column = 0u; column < width; ++column) {
            const auto u_delta = static_cast<std::uint8_t>(column);
            const auto v_delta = static_cast<std::uint8_t>(row);
            const auto u = static_cast<std::uint8_t>(
                flip_x ? static_cast<std::uint8_t>(base_u - u_delta)
                       : static_cast<std::uint8_t>(base_u + u_delta));
            const auto v = static_cast<std::uint8_t>(
                flip_y ? static_cast<std::uint8_t>(base_v - v_delta)
                       : static_cast<std::uint8_t>(base_v + v_delta));
            const auto texel = psx_gpu_texture_detail::sample_texture(bus, u, v, clut);
            if (texel == 0u) continue;
            const auto pixel = psx_gpu_texture_detail::modulate_texture(texel, command);
            psx_gpu_write_render_pixel(
                bus,
                origin_x + static_cast<std::int32_t>(column),
                origin_y + static_cast<std::int32_t>(row),
                pixel);
        }
    }
}
