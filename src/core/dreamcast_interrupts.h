#pragma once

#include "core/result.h"
#include "core/sh4_reference_executor.h"

namespace jojo {

class DreamcastSystemAsic;

[[nodiscard]] Result<bool> service_dreamcast_system_irq(
    DreamcastSystemAsic& asic,
    Sh4ReferenceState& state);

[[nodiscard]] Sh4ReferenceBlockBoundaryHook make_dreamcast_system_irq_boundary_hook(
    DreamcastSystemAsic& asic);

}
