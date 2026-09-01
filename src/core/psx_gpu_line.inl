namespace psx_gpu_line_detail {

[[nodiscard]] inline bool is_polyline_terminator(std::uint32_t value) noexcept {
    return (value & 0xf000f000u) == 0x50005000u;
}

inline void raster_line(PsxBus& bus,
                        psx_gpu_polygon_detail::Vertex a,
                        psx_gpu_polygon_detail::Vertex b,
                        psx_gpu_polygon_detail::Color color_a,
                        psx_gpu_polygon_detail::Color color_b,
                        bool gouraud,
                        bool semi_transparent) noexcept {
    const auto dx = std::abs(b.x - a.x);
    const auto dy = std::abs(b.y - a.y);
    if (dx > 1023 || dy > 511) return;

    const auto steps = std::max(dx, dy);
    auto x = a.x;
    auto y = a.y;
    const auto sx = a.x < b.x ? 1 : (a.x > b.x ? -1 : 0);
    const auto sy = a.y < b.y ? 1 : (a.y > b.y ? -1 : 0);
    auto error = dx - dy;

    for (std::int32_t step = 0;; ++step) {
        auto color = color_a;
        if (gouraud && steps != 0) {
            const auto interpolate = [step, steps](std::uint32_t from,
                                                   std::uint32_t to) noexcept {
                const auto numerator =
                    static_cast<std::int64_t>(from) * (steps - step) +
                    static_cast<std::int64_t>(to) * step;
                return static_cast<std::uint32_t>(numerator / steps);
            };
            color.red = interpolate(color_a.red, color_b.red);
            color.green = interpolate(color_a.green, color_b.green);
            color.blue = interpolate(color_a.blue, color_b.blue);
        }
        const auto rgb24 = color.red | (color.green << 8u) | (color.blue << 16u);
        psx_gpu_write_render_pixel(
            bus, x, y, psx_gpu_bgr555(rgb24), semi_transparent);

        if (x == b.x && y == b.y) break;
        const auto doubled = error * 2;
        if (doubled > -dy) {
            error -= dy;
            x += sx;
        }
        if (doubled < dx) {
            error += dx;
            y += sy;
        }
    }
}

inline void draw_segment(PsxBus& bus,
                         std::uint32_t command,
                         std::uint32_t vertex_a,
                         std::uint32_t vertex_b,
                         std::uint32_t color_a_word,
                         std::uint32_t color_b_word,
                         bool gouraud) noexcept {
    const bool semi_transparent = (command & (1u << 25u)) != 0u;
    const auto a = psx_gpu_polygon_detail::decode_vertex(bus, vertex_a);
    const auto b = psx_gpu_polygon_detail::decode_vertex(bus, vertex_b);
    const auto color_a = psx_gpu_polygon_detail::decode_color(color_a_word);
    const auto color_b = gouraud
                             ? psx_gpu_polygon_detail::decode_color(color_b_word)
                             : color_a;
    raster_line(bus, a, b, color_a, color_b, gouraud, semi_transparent);
}

} // namespace psx_gpu_line_detail

inline void psx_gpu_execute_line(PsxBus& bus) noexcept {
    const auto command = bus.gpu_gp0_packet[0];
    const bool gouraud = (command & (1u << 28u)) != 0u;
    if (gouraud) {
        psx_gpu_line_detail::draw_segment(
            bus,
            command,
            bus.gpu_gp0_packet[1],
            bus.gpu_gp0_packet[3],
            command,
            bus.gpu_gp0_packet[2],
            true);
    } else {
        psx_gpu_line_detail::draw_segment(
            bus,
            command,
            bus.gpu_gp0_packet[1],
            bus.gpu_gp0_packet[2],
            command,
            command,
            false);
    }
}

inline bool psx_gpu_consume_polyline_word(PsxBus& bus,
                                          std::uint32_t value) noexcept {
    if (!bus.gpu_polyline_active) return false;

    if (!bus.gpu_polyline_gouraud) {
        if (psx_gpu_line_detail::is_polyline_terminator(value)) {
            bus.gpu_polyline_active = false;
            bus.gpu_gp0_packet = {};
            bus.gpu_gp0_packet_count = 0u;
            bus.gpu_gp0_packet_words = 0u;
            return true;
        }
        psx_gpu_line_detail::draw_segment(
            bus,
            bus.gpu_gp0_packet[0],
            bus.gpu_gp0_packet[1],
            value,
            bus.gpu_gp0_packet[0],
            bus.gpu_gp0_packet[0],
            false);
        bus.gpu_gp0_packet[1] = value;
        return true;
    }

    if (bus.gpu_polyline_expect_color) {
        if (psx_gpu_line_detail::is_polyline_terminator(value)) {
            bus.gpu_polyline_active = false;
            bus.gpu_polyline_expect_color = false;
            bus.gpu_gp0_packet = {};
            bus.gpu_gp0_packet_count = 0u;
            bus.gpu_gp0_packet_words = 0u;
            return true;
        }
        bus.gpu_gp0_packet[3] = value;
        bus.gpu_polyline_expect_color = false;
        return true;
    }

    psx_gpu_line_detail::draw_segment(
        bus,
        bus.gpu_gp0_packet[0],
        bus.gpu_gp0_packet[1],
        value,
        bus.gpu_gp0_packet[2],
        bus.gpu_gp0_packet[3],
        true);
    bus.gpu_gp0_packet[1] = value;
    bus.gpu_gp0_packet[2] = bus.gpu_gp0_packet[3];
    bus.gpu_polyline_expect_color = true;
    return true;
}
