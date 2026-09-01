#pragma once

#include "core/presentation.h"
#include "core/psx_video.h"
#include "core/result.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace jojo {

struct PsxPresentedFrame {
    std::uint32_t width{};
    std::uint32_t height{};
    RectI viewport{};
    std::vector<std::uint32_t> rgba{};
};

[[nodiscard]] inline Result<PsxPresentedFrame> compose_psx_presentation_frame(
    const PsxVideoFrame& source,
    const PresentationPlan& presentation) {
    if (source.width == 0u || source.height == 0u) {
        return Result<PsxPresentedFrame>::failure(
            ErrorCode::invalid_argument,
            "PS1 source frame dimensions must be non-zero");
    }
    const auto source_pixels =
        static_cast<std::uint64_t>(source.width) * source.height;
    if (source_pixels != source.rgba.size()) {
        return Result<PsxPresentedFrame>::failure(
            ErrorCode::invalid_argument,
            "PS1 source frame pixel count does not match its dimensions");
    }
    if (presentation.presentation_resolution.width == 0u ||
        presentation.presentation_resolution.height == 0u) {
        return Result<PsxPresentedFrame>::failure(
            ErrorCode::invalid_argument,
            "presentation resolution must be non-zero");
    }
    if (presentation.viewport.width == 0u || presentation.viewport.height == 0u) {
        return Result<PsxPresentedFrame>::failure(
            ErrorCode::invalid_argument,
            "presentation viewport must be non-zero");
    }

    const auto output_pixels =
        static_cast<std::uint64_t>(presentation.presentation_resolution.width) *
        presentation.presentation_resolution.height;
    if (output_pixels > std::numeric_limits<std::size_t>::max()) {
        return Result<PsxPresentedFrame>::failure(
            ErrorCode::invalid_argument,
            "presentation frame is too large for this host");
    }

    PsxPresentedFrame output{};
    output.width = presentation.presentation_resolution.width;
    output.height = presentation.presentation_resolution.height;
    output.viewport = presentation.viewport;
    output.rgba.assign(static_cast<std::size_t>(output_pixels), 0xff000000u);

    const auto viewport_left = static_cast<std::int64_t>(presentation.viewport.x);
    const auto viewport_top = static_cast<std::int64_t>(presentation.viewport.y);
    const auto viewport_right =
        viewport_left + static_cast<std::int64_t>(presentation.viewport.width);
    const auto viewport_bottom =
        viewport_top + static_cast<std::int64_t>(presentation.viewport.height);

    const auto clip_left = std::max<std::int64_t>(0, viewport_left);
    const auto clip_top = std::max<std::int64_t>(0, viewport_top);
    const auto clip_right = std::min<std::int64_t>(
        static_cast<std::int64_t>(output.width), viewport_right);
    const auto clip_bottom = std::min<std::int64_t>(
        static_cast<std::int64_t>(output.height), viewport_bottom);

    if (clip_left >= clip_right || clip_top >= clip_bottom) {
        return Result<PsxPresentedFrame>::success(std::move(output));
    }

    for (auto y = clip_top; y < clip_bottom; ++y) {
        const auto local_y = static_cast<std::uint64_t>(y - viewport_top);
        const auto source_y = static_cast<std::uint32_t>(
            (local_y * source.height) / presentation.viewport.height);
        for (auto x = clip_left; x < clip_right; ++x) {
            const auto local_x = static_cast<std::uint64_t>(x - viewport_left);
            const auto source_x = static_cast<std::uint32_t>(
                (local_x * source.width) / presentation.viewport.width);
            output.rgba[
                static_cast<std::size_t>(y) * output.width +
                static_cast<std::size_t>(x)] =
                source.rgba[
                    static_cast<std::size_t>(source_y) * source.width + source_x];
        }
    }

    return Result<PsxPresentedFrame>::success(std::move(output));
}

} // namespace jojo
