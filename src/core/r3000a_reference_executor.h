#pragma once

#include "core/r3000a_bus.h"
#include "core/r3000a_diagnostics.h"
#include "core/r3000a_state.h"

namespace jojo {

[[nodiscard]] R3000aStepResult step_r3000a(R3000aState& state, R3000aBus& bus) noexcept;

} // namespace jojo
