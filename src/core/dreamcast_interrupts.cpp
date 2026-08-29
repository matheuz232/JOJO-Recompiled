#include "core/dreamcast_interrupts.h"

#include "core/dreamcast_system_asic.h"
#include "core/sh4_reference_executor.h"

namespace jojo {

Result<bool> service_dreamcast_system_irq(DreamcastSystemAsic& asic,
                                          Sh4ReferenceState& state) {
    const auto pending = asic.pending_irq_level();
    if (!pending.has_value()) {
        return Result<bool>::success(false);
    }
    return accept_sh4_irl_interrupt(state, *pending);
}

}
