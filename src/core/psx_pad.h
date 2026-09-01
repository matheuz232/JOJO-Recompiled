#pragma once

#include "core/input.h"
#include <array>
#include <cstdint>

namespace jojo {

struct PsxDigitalPadState {
    // Standard PlayStation digital-pad button word. Hardware reports buttons
    // active-low, so an idle controller is FFFFh.
    std::uint16_t buttons{0xffffu};
    friend bool operator==(const PsxDigitalPadState&, const PsxDigitalPadState&) = default;
};

using PsxDigitalPadFrame = std::array<PsxDigitalPadState, input_player_count>;
using PsxDigitalPadResponse = std::array<std::uint8_t, 5>;

[[nodiscard]] PsxDigitalPadState make_psx_digital_pad_state(
    const ResolvedPlayerInput& input) noexcept;
[[nodiscard]] PsxDigitalPadFrame make_psx_digital_pad_frame(
    const ResolvedInputFrame& input) noexcept;
[[nodiscard]] PsxDigitalPadResponse psx_digital_pad_poll_response(
    const PsxDigitalPadState& pad) noexcept;

}
