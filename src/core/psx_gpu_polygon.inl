namespace psx_gpu_polygon_detail {

struct Vertex {
    std::int32_t x{};
    std::int32_t y{};
};

struct Color {
    std::uint32_t red{};
    std::uint32_t green{};
    std::uint32_t blue{};
};

struct Uv {
    std::uint32_t u{};
    std::uint32_t v{};
};

[[nodiscard]] inline Vertex decode_vertex(const PsxBus& bus,
                                          std::uint32_t packed) noexcept {
    const auto offset_x = psx_gpu_sign_extend_11(bus.gpu_drawing_offset);
    const auto offset_y = psx_gpu_sign_extend_11(bus.gpu_drawing_offset >> 11u);
    return {
        psx_gpu_sign_extend_11(packed) + offset_x,
        psx_gpu_sign_extend_11(packed >> 16u) + offset_y,
    };
}

[[nodiscard]] inline Color decode_color(std::uint32_t packed) noexcept {
    return {
        packed & 0xffu,
        (packed >> 8u) & 0xffu,
        (packed >> 16u) & 0xffu,
    };
}

[[nodiscard]] inline Uv decode_uv(std::uint32_t packed) noexcept {
    return {packed & 0xffu, (packed >> 8u) & 0xffu};
}

inline void apply_polygon_texpage(PsxBus& bus, std::uint16_t texpage) noexcept {
    constexpr std::uint32_t polygon_texpage_draw_mode_bits = 0x000009ffu;
    bus.gpu_draw_mode =
        (bus.gpu_draw_mode & ~polygon_texpage_draw_mode_bits) |
        (static_cast<std::uint32_t>(texpage) & polygon_texpage_draw_mode_bits);
    bus.gpu_status =
        (bus.gpu_status & ~(0x000001ffu | (1u << 15u))) |
        (static_cast<std::uint32_t>(texpage) & 0x000001ffu) |
        (((static_cast<std::uint32_t>(texpage) >> 11u) & 1u) << 15u);
}

[[nodiscard]] inline std::int64_t edge(std::int64_t ax,
                                       std::int64_t ay,
                                       std::int64_t bx,
                                       std::int64_t by,
                                       std::int64_t px,
                                       std::int64_t py) noexcept {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

[[nodiscard]] inline std::uint32_t interpolate_attribute(
    std::int64_t area,
    std::int64_t e0,
    std::int64_t e1,
    std::int64_t e2,
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c) noexcept {
    const auto numerator =
        static_cast<std::int64_t>(a) * e1 +
        static_cast<std::int64_t>(b) * e2 +
        static_cast<std::int64_t>(c) * e0;
    return static_cast<std::uint32_t>(numerator / area);
}

inline void raster_flat_triangle(PsxBus& bus,
                                 Vertex a,
                                 Vertex b,
                                 Vertex c,
                                 std::uint16_t color,
                                 bool semi_transparent) noexcept {
    const auto min_x = std::min({a.x, b.x, c.x});
    const auto max_x = std::max({a.x, b.x, c.x});
    const auto min_y = std::min({a.y, b.y, c.y});
    const auto max_y = std::max({a.y, b.y, c.y});
    if (max_x - min_x > 1023 || max_y - min_y > 511) return;

    const std::int64_t ax = static_cast<std::int64_t>(a.x) * 2;
    const std::int64_t ay = static_cast<std::int64_t>(a.y) * 2;
    const std::int64_t bx = static_cast<std::int64_t>(b.x) * 2;
    const std::int64_t by = static_cast<std::int64_t>(b.y) * 2;
    const std::int64_t cx = static_cast<std::int64_t>(c.x) * 2;
    const std::int64_t cy = static_cast<std::int64_t>(c.y) * 2;
    const auto area = edge(ax, ay, bx, by, cx, cy);
    if (area == 0) return;

    const auto start_x = std::max<std::int32_t>(0, min_x);
    const auto end_x = std::min<std::int32_t>(
        static_cast<std::int32_t>(PsxBus::gpu_vram_width), max_x);
    const auto start_y = std::max<std::int32_t>(0, min_y);
    const auto end_y = std::min<std::int32_t>(
        static_cast<std::int32_t>(PsxBus::gpu_vram_height), max_y);

    for (auto y = start_y; y < end_y; ++y) {
        const auto py = static_cast<std::int64_t>(y) * 2 + 1;
        for (auto x = start_x; x < end_x; ++x) {
            const auto px = static_cast<std::int64_t>(x) * 2 + 1;
            const auto e0 = edge(ax, ay, bx, by, px, py);
            const auto e1 = edge(bx, by, cx, cy, px, py);
            const auto e2 = edge(cx, cy, ax, ay, px, py);
            const bool inside = area > 0
                                    ? (e0 >= 0 && e1 >= 0 && e2 >= 0)
                                    : (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (!inside) continue;
            psx_gpu_write_render_pixel(bus, x, y, color, semi_transparent);
        }
    }
}

inline void raster_gouraud_triangle(PsxBus& bus,
                                    Vertex a,
                                    Vertex b,
                                    Vertex c,
                                    Color color_a,
                                    Color color_b,
                                    Color color_c,
                                    bool semi_transparent) noexcept {
    const auto min_x = std::min({a.x, b.x, c.x});
    const auto max_x = std::max({a.x, b.x, c.x});
    const auto min_y = std::min({a.y, b.y, c.y});
    const auto max_y = std::max({a.y, b.y, c.y});
    if (max_x - min_x > 1023 || max_y - min_y > 511) return;

    const std::int64_t ax = static_cast<std::int64_t>(a.x) * 2;
    const std::int64_t ay = static_cast<std::int64_t>(a.y) * 2;
    const std::int64_t bx = static_cast<std::int64_t>(b.x) * 2;
    const std::int64_t by = static_cast<std::int64_t>(b.y) * 2;
    const std::int64_t cx = static_cast<std::int64_t>(c.x) * 2;
    const std::int64_t cy = static_cast<std::int64_t>(c.y) * 2;
    const auto area = edge(ax, ay, bx, by, cx, cy);
    if (area == 0) return;

    const auto start_x = std::max<std::int32_t>(0, min_x);
    const auto end_x = std::min<std::int32_t>(
        static_cast<std::int32_t>(PsxBus::gpu_vram_width), max_x);
    const auto start_y = std::max<std::int32_t>(0, min_y);
    const auto end_y = std::min<std::int32_t>(
        static_cast<std::int32_t>(PsxBus::gpu_vram_height), max_y);

    for (auto y = start_y; y < end_y; ++y) {
        const auto py = static_cast<std::int64_t>(y) * 2 + 1;
        for (auto x = start_x; x < end_x; ++x) {
            const auto px = static_cast<std::int64_t>(x) * 2 + 1;
            const auto e0 = edge(ax, ay, bx, by, px, py);
            const auto e1 = edge(bx, by, cx, cy, px, py);
            const auto e2 = edge(cx, cy, ax, ay, px, py);
            const bool inside = area > 0
                                    ? (e0 >= 0 && e1 >= 0 && e2 >= 0)
                                    : (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (!inside) continue;

            const auto red = interpolate_attribute(
                area, e0, e1, e2, color_a.red, color_b.red, color_c.red);
            const auto green = interpolate_attribute(
                area, e0, e1, e2, color_a.green, color_b.green, color_c.green);
            const auto blue = interpolate_attribute(
                area, e0, e1, e2, color_a.blue, color_b.blue, color_c.blue);
            const auto rgb24 = red | (green << 8u) | (blue << 16u);
            psx_gpu_write_render_pixel(
                bus, x, y, psx_gpu_bgr555(rgb24), semi_transparent);
        }
    }
}

inline void raster_textured_triangle(PsxBus& bus,
                                     Vertex a,
                                     Vertex b,
                                     Vertex c,
                                     Uv uv_a,
                                     Uv uv_b,
                                     Uv uv_c,
                                     Color color_a,
                                     Color color_b,
                                     Color color_c,
                                     bool gouraud,
                                     std::uint16_t clut,
                                     std::uint32_t command) noexcept {
    const auto min_x = std::min({a.x, b.x, c.x});
    const auto max_x = std::max({a.x, b.x, c.x});
    const auto min_y = std::min({a.y, b.y, c.y});
    const auto max_y = std::max({a.y, b.y, c.y});
    if (max_x - min_x > 1023 || max_y - min_y > 511) return;

    const std::int64_t ax = static_cast<std::int64_t>(a.x) * 2;
    const std::int64_t ay = static_cast<std::int64_t>(a.y) * 2;
    const std::int64_t bx = static_cast<std::int64_t>(b.x) * 2;
    const std::int64_t by = static_cast<std::int64_t>(b.y) * 2;
    const std::int64_t cx = static_cast<std::int64_t>(c.x) * 2;
    const std::int64_t cy = static_cast<std::int64_t>(c.y) * 2;
    const auto area = edge(ax, ay, bx, by, cx, cy);
    if (area == 0) return;

    const auto start_x = std::max<std::int32_t>(0, min_x);
    const auto end_x = std::min<std::int32_t>(
        static_cast<std::int32_t>(PsxBus::gpu_vram_width), max_x);
    const auto start_y = std::max<std::int32_t>(0, min_y);
    const auto end_y = std::min<std::int32_t>(
        static_cast<std::int32_t>(PsxBus::gpu_vram_height), max_y);
    const bool raw_texture = (command & (1u << 24u)) != 0u;
    const bool primitive_semi_transparent = (command & (1u << 25u)) != 0u;

    for (auto y = start_y; y < end_y; ++y) {
        const auto py = static_cast<std::int64_t>(y) * 2 + 1;
        for (auto x = start_x; x < end_x; ++x) {
            const auto px = static_cast<std::int64_t>(x) * 2 + 1;
            const auto e0 = edge(ax, ay, bx, by, px, py);
            const auto e1 = edge(bx, by, cx, cy, px, py);
            const auto e2 = edge(cx, cy, ax, ay, px, py);
            const bool inside = area > 0
                                    ? (e0 >= 0 && e1 >= 0 && e2 >= 0)
                                    : (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (!inside) continue;

            const auto u = static_cast<std::uint8_t>(interpolate_attribute(
                area, e0, e1, e2, uv_a.u, uv_b.u, uv_c.u));
            const auto v = static_cast<std::uint8_t>(interpolate_attribute(
                area, e0, e1, e2, uv_a.v, uv_b.v, uv_c.v));
            const auto texel = psx_gpu_texture_detail::sample_texture(bus, u, v, clut);
            if (texel == 0u) continue;

            auto pixel = texel;
            if (!raw_texture) {
                std::uint32_t rgb24 = command & 0x00ffffffu;
                if (gouraud) {
                    const auto red = interpolate_attribute(
                        area, e0, e1, e2, color_a.red, color_b.red, color_c.red);
                    const auto green = interpolate_attribute(
                        area, e0, e1, e2, color_a.green, color_b.green, color_c.green);
                    const auto blue = interpolate_attribute(
                        area, e0, e1, e2, color_a.blue, color_b.blue, color_c.blue);
                    rgb24 = red | (green << 8u) | (blue << 16u);
                }
                pixel = psx_gpu_texture_detail::modulate_texture_rgb(texel, rgb24);
            }
            const bool blend =
                primitive_semi_transparent && (texel & 0x8000u) != 0u;
            psx_gpu_write_render_pixel(bus, x, y, pixel, blend);
        }
    }
}

} // namespace psx_gpu_polygon_detail

inline void psx_gpu_execute_flat_polygon(PsxBus& bus) noexcept {
    const auto command = bus.gpu_gp0_packet[0];
    const bool quad = (command & (1u << 27u)) != 0u;
    const bool semi_transparent = (command & (1u << 25u)) != 0u;
    const auto color = psx_gpu_bgr555(command & 0x00ffffffu);

    const auto v1 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[1]);
    const auto v2 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[2]);
    const auto v3 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[3]);
    psx_gpu_polygon_detail::raster_flat_triangle(
        bus, v1, v2, v3, color, semi_transparent);

    if (quad) {
        const auto v4 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[4]);
        psx_gpu_polygon_detail::raster_flat_triangle(
            bus, v2, v3, v4, color, semi_transparent);
    }
}

inline void psx_gpu_execute_gouraud_polygon(PsxBus& bus) noexcept {
    const auto command = bus.gpu_gp0_packet[0];
    const bool quad = (command & (1u << 27u)) != 0u;
    const bool semi_transparent = (command & (1u << 25u)) != 0u;

    const auto c1 = psx_gpu_polygon_detail::decode_color(command);
    const auto v1 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[1]);
    const auto c2 = psx_gpu_polygon_detail::decode_color(bus.gpu_gp0_packet[2]);
    const auto v2 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[3]);
    const auto c3 = psx_gpu_polygon_detail::decode_color(bus.gpu_gp0_packet[4]);
    const auto v3 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[5]);
    psx_gpu_polygon_detail::raster_gouraud_triangle(
        bus, v1, v2, v3, c1, c2, c3, semi_transparent);

    if (quad) {
        const auto c4 = psx_gpu_polygon_detail::decode_color(bus.gpu_gp0_packet[6]);
        const auto v4 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[7]);
        psx_gpu_polygon_detail::raster_gouraud_triangle(
            bus, v2, v3, v4, c2, c3, c4, semi_transparent);
    }
}

inline void psx_gpu_execute_textured_polygon(PsxBus& bus) noexcept {
    const auto command = bus.gpu_gp0_packet[0];
    const bool gouraud = (command & (1u << 28u)) != 0u;
    const bool quad = (command & (1u << 27u)) != 0u;

    if (!gouraud) {
        const auto v1 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[1]);
        const auto uv1_word = bus.gpu_gp0_packet[2];
        const auto uv1 = psx_gpu_polygon_detail::decode_uv(uv1_word);
        const auto v2 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[3]);
        const auto uv2_word = bus.gpu_gp0_packet[4];
        const auto uv2 = psx_gpu_polygon_detail::decode_uv(uv2_word);
        const auto v3 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[5]);
        const auto uv3 = psx_gpu_polygon_detail::decode_uv(bus.gpu_gp0_packet[6]);
        const auto clut = static_cast<std::uint16_t>(uv1_word >> 16u);
        psx_gpu_polygon_detail::apply_polygon_texpage(
            bus, static_cast<std::uint16_t>(uv2_word >> 16u));
        const auto color = psx_gpu_polygon_detail::decode_color(command);
        psx_gpu_polygon_detail::raster_textured_triangle(
            bus, v1, v2, v3, uv1, uv2, uv3,
            color, color, color, false, clut, command);

        if (quad) {
            const auto v4 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[7]);
            const auto uv4 = psx_gpu_polygon_detail::decode_uv(bus.gpu_gp0_packet[8]);
            psx_gpu_polygon_detail::raster_textured_triangle(
                bus, v2, v3, v4, uv2, uv3, uv4,
                color, color, color, false, clut, command);
        }
        return;
    }

    const auto c1 = psx_gpu_polygon_detail::decode_color(command);
    const auto v1 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[1]);
    const auto uv1_word = bus.gpu_gp0_packet[2];
    const auto uv1 = psx_gpu_polygon_detail::decode_uv(uv1_word);
    const auto c2 = psx_gpu_polygon_detail::decode_color(bus.gpu_gp0_packet[3]);
    const auto v2 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[4]);
    const auto uv2_word = bus.gpu_gp0_packet[5];
    const auto uv2 = psx_gpu_polygon_detail::decode_uv(uv2_word);
    const auto c3 = psx_gpu_polygon_detail::decode_color(bus.gpu_gp0_packet[6]);
    const auto v3 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[7]);
    const auto uv3 = psx_gpu_polygon_detail::decode_uv(bus.gpu_gp0_packet[8]);
    const auto clut = static_cast<std::uint16_t>(uv1_word >> 16u);
    psx_gpu_polygon_detail::apply_polygon_texpage(
        bus, static_cast<std::uint16_t>(uv2_word >> 16u));
    psx_gpu_polygon_detail::raster_textured_triangle(
        bus, v1, v2, v3, uv1, uv2, uv3,
        c1, c2, c3, true, clut, command);

    if (quad) {
        const auto c4 = psx_gpu_polygon_detail::decode_color(bus.gpu_gp0_packet[9]);
        const auto v4 = psx_gpu_polygon_detail::decode_vertex(bus, bus.gpu_gp0_packet[10]);
        const auto uv4 = psx_gpu_polygon_detail::decode_uv(bus.gpu_gp0_packet[11]);
        psx_gpu_polygon_detail::raster_textured_triangle(
            bus, v2, v3, v4, uv2, uv3, uv4,
            c2, c3, c4, true, clut, command);
    }
}
