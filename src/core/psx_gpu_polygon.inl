namespace psx_gpu_polygon_detail {

struct Vertex {
    std::int32_t x{};
    std::int32_t y{};
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

[[nodiscard]] inline std::int64_t edge(std::int64_t ax,
                                       std::int64_t ay,
                                       std::int64_t bx,
                                       std::int64_t by,
                                       std::int64_t px,
                                       std::int64_t py) noexcept {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
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
