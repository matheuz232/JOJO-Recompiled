#pragma once

#include "core/psx_bus.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

struct PsxVideoFrame {
    std::uint32_t width{};
    std::uint32_t height{};
    bool display_enabled{};
    bool rgb24{};
    std::vector<std::uint32_t> rgba{};
};

[[nodiscard]] inline std::uint32_t psx_video_width(std::uint8_t display_mode) noexcept {
    if ((display_mode & 0x40u) != 0u) return 368u;
    switch (display_mode & 0x03u) {
    case 0u: return 256u;
    case 1u: return 320u;
    case 2u: return 512u;
    default: return 640u;
    }
}

[[nodiscard]] inline std::uint32_t psx_video_height(std::uint8_t display_mode) noexcept {
    return (display_mode & 0x04u) != 0u ? 480u : 240u;
}

[[nodiscard]] inline std::uint8_t psx_video_expand_5(std::uint16_t value) noexcept {
    const auto channel = static_cast<std::uint8_t>(value & 0x1fu);
    return static_cast<std::uint8_t>((channel << 3u) | (channel >> 2u));
}

[[nodiscard]] inline std::uint32_t psx_video_bgr555_to_rgba(std::uint16_t pixel) noexcept {
    const auto red = static_cast<std::uint32_t>(psx_video_expand_5(pixel));
    const auto green = static_cast<std::uint32_t>(psx_video_expand_5(pixel >> 5u));
    const auto blue = static_cast<std::uint32_t>(psx_video_expand_5(pixel >> 10u));
    return red | (green << 8u) | (blue << 16u) | 0xff000000u;
}

[[nodiscard]] inline Result<PsxVideoFrame> capture_psx_video_frame(const PsxBus& bus) {
    const auto mode = bus.gpu_display_mode;
    const bool rgb24 = (mode & 0x10u) != 0u;
    if (rgb24) {
        return Result<PsxVideoFrame>::failure(
            ErrorCode::backend_unavailable,
            "24-bit PS1 display capture is not implemented");
    }

    PsxVideoFrame frame{};
    frame.width = psx_video_width(mode);
    frame.height = psx_video_height(mode);
    frame.display_enabled =
        (bus.gpu_status & PsxBus::gpu_status_display_disabled) == 0u;
    frame.rgb24 = false;
    frame.rgba.assign(
        static_cast<std::size_t>(frame.width) * frame.height,
        0xff000000u);

    if (!frame.display_enabled) {
        return Result<PsxVideoFrame>::success(std::move(frame));
    }

    const auto start_x = bus.gpu_display_vram_start & 0x3ffu;
    const auto start_y = (bus.gpu_display_vram_start >> 10u) & 0x1ffu;

    for (std::uint32_t y = 0u; y < frame.height; ++y) {
        const auto source_y = (start_y + y) & 0x1ffu;
        for (std::uint32_t x = 0u; x < frame.width; ++x) {
            const auto source_x = (start_x + x) & 0x3ffu;
            const auto pixel = bus.gpu_vram[
                static_cast<std::size_t>(source_y) * PsxBus::gpu_vram_width + source_x];
            frame.rgba[static_cast<std::size_t>(y) * frame.width + x] =
                psx_video_bgr555_to_rgba(pixel);
        }
    }

    return Result<PsxVideoFrame>::success(std::move(frame));
}

} // namespace jojo
