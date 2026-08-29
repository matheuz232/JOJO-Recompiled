#pragma once

#include "core/result.h"

namespace jojo {

class DreamcastSystemAsic;
struct Sh4ReferenceState;

[[nodiscard]] Result<bool> service_dreamcast_system_irq(
    DreamcastSystemAsic& asic,
    Sh4ReferenceState& state);

}
