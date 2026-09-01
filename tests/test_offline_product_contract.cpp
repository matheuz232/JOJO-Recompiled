#include "core/input.h"
#include "core/presentation.h"
#include "core/runtime.h"

#include <array>
#include <cassert>
#include <filesystem>

int main() {
    static_assert(jojo::input_player_count == 2);

    const jojo::RendererCapabilities caps{
        .exclusive_fullscreen = true,
        .texture_filters = {jojo::TextureFilter::off, jojo::TextureFilter::x16},
        .msaa_modes = {jojo::Msaa::off, jojo::Msaa::x4},
    };
    const jojo::PresentationInputs inputs{
        .simulation_resolution = {640u, 480u},
        .desktop_resolution = {3840u, 2160u},
        .dpi = 96u,
    };

    constexpr std::array aspects{
        jojo::AspectRatio::ratio_4_3,
        jojo::AspectRatio::ratio_16_9,
        jojo::AspectRatio::ratio_16_10,
        jojo::AspectRatio::ratio_21_9,
        jojo::AspectRatio::ratio_32_9,
    };

    for (const auto aspect : aspects) {
        jojo::GraphicsSettings graphics{};
        graphics.width = 3840;
        graphics.height = 2160;
        graphics.aspect_ratio = aspect;
        const auto plan = jojo::build_presentation_plan(graphics, inputs, caps);
        assert(plan);
        assert(plan.value.presentation_resolution.width == 3840u);
        assert(plan.value.presentation_resolution.height == 2160u);
        assert(plan.value.viewport.width > 0u);
        assert(plan.value.viewport.height > 0u);
        assert(plan.value.uniform_presentation_scale);
    }

    const auto missing = jojo::load_prepared_psx_runtime(
        std::filesystem::temp_directory_path() / "jojo_recompiled_missing_runtime_contract");
    assert(!missing);
    assert(missing.error == jojo::ErrorCode::invalid_installation);

    jojo::PsxRuntime runtime{};
    jojo::reset_psx_r3000a(runtime.cpu, 0x1f801000u);
    jojo::ResolvedInputFrame frame{};
    frame[0].actions[jojo::GameAction::attack_light] = true;
    frame[1].actions[jojo::GameAction::attack_heavy] = true;

    const auto slice = jojo::run_psx_runtime_slice(runtime, frame, 64u);
    assert(slice.executed_steps == 1u);
    assert(!slice.reached_budget);
    assert(slice.last_step.reason == jojo::PsxR3000aStepReason::memory_fault);
    assert(slice.last_step.instruction_pc == 0x1f801000u);
    assert((runtime.bus.sio0.pads[0].buttons & (1u << 15u)) == 0u);
    assert((runtime.bus.sio0.pads[1].buttons & (1u << 13u)) == 0u);
}
