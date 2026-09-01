#include "core/psx_frame_compositor.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void compositor_fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d PSX compositor contract failed: %s\n",
                 __FILE__, line, expression);
    std::exit(1);
}

#define COMPOSITOR_REQUIRE(expr) do { if (!(expr)) compositor_fail(#expr, __LINE__); } while (0)

std::uint32_t pixel_at(const jojo::PsxPresentedFrame& frame,
                       std::uint32_t x,
                       std::uint32_t y) {
    return frame.rgba[static_cast<std::size_t>(y) * frame.width + x];
}

jojo::PsxVideoFrame make_source() {
    jojo::PsxVideoFrame frame{};
    frame.width = 2u;
    frame.height = 2u;
    frame.display_enabled = true;
    frame.rgba = {
        0xff0000ffu, 0xff00ff00u,
        0xffff0000u, 0xffffffffu,
    };
    return frame;
}

void test_nearest_compositor_respects_viewport_and_black_bars() {
    const auto source = make_source();
    jojo::PresentationPlan plan{};
    plan.presentation_resolution = {6u, 4u};
    plan.viewport = {1, 1, 4u, 2u};

    const auto composed = jojo::compose_psx_presentation_frame(source, plan);
    COMPOSITOR_REQUIRE(composed);
    COMPOSITOR_REQUIRE(composed.value.width == 6u);
    COMPOSITOR_REQUIRE(composed.value.height == 4u);
    COMPOSITOR_REQUIRE(composed.value.viewport == plan.viewport);
    COMPOSITOR_REQUIRE(composed.value.rgba.size() == 24u);

    COMPOSITOR_REQUIRE(pixel_at(composed.value, 0u, 0u) == 0xff000000u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 5u, 3u) == 0xff000000u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 1u, 1u) == 0xff0000ffu);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 2u, 1u) == 0xff0000ffu);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 3u, 1u) == 0xff00ff00u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 4u, 1u) == 0xff00ff00u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 1u, 2u) == 0xffff0000u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 4u, 2u) == 0xffffffffu);
}

void test_viewport_clips_source_without_out_of_bounds_writes() {
    const auto source = make_source();
    jojo::PresentationPlan plan{};
    plan.presentation_resolution = {3u, 3u};
    plan.viewport = {-1, -1, 4u, 4u};

    const auto composed = jojo::compose_psx_presentation_frame(source, plan);
    COMPOSITOR_REQUIRE(composed);
    COMPOSITOR_REQUIRE(composed.value.rgba.size() == 9u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 0u, 0u) == 0xff0000ffu);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 2u, 0u) == 0xff00ff00u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 0u, 2u) == 0xffff0000u);
    COMPOSITOR_REQUIRE(pixel_at(composed.value, 2u, 2u) == 0xffffffffu);
}

void test_invalid_frame_and_presentation_are_rejected() {
    jojo::PresentationPlan plan{};
    plan.presentation_resolution = {640u, 480u};
    plan.viewport = {0, 0, 640u, 480u};

    jojo::PsxVideoFrame invalid{};
    invalid.width = 2u;
    invalid.height = 2u;
    invalid.rgba.resize(3u);
    auto result = jojo::compose_psx_presentation_frame(invalid, plan);
    COMPOSITOR_REQUIRE(!result);
    COMPOSITOR_REQUIRE(result.error == jojo::ErrorCode::invalid_argument);

    auto source = make_source();
    plan.presentation_resolution = {0u, 480u};
    result = jojo::compose_psx_presentation_frame(source, plan);
    COMPOSITOR_REQUIRE(!result);
    COMPOSITOR_REQUIRE(result.error == jojo::ErrorCode::invalid_argument);

    plan.presentation_resolution = {640u, 480u};
    plan.viewport = {0, 0, 0u, 480u};
    result = jojo::compose_psx_presentation_frame(source, plan);
    COMPOSITOR_REQUIRE(!result);
    COMPOSITOR_REQUIRE(result.error == jojo::ErrorCode::invalid_argument);
}

struct CompositorContractRunner {
    CompositorContractRunner() {
        test_nearest_compositor_respects_viewport_and_black_bars();
        test_viewport_clips_source_without_out_of_bounds_writes();
        test_invalid_frame_and_presentation_are_rejected();
    }
};

CompositorContractRunner compositor_contract_runner{};

} // namespace
