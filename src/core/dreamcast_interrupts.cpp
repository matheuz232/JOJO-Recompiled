#include "core/dreamcast_interrupts.h"

#include "core/dreamcast_system_asic.h"

namespace jojo {

Result<bool> service_dreamcast_system_irq(DreamcastSystemAsic& asic,
                                          Sh4ReferenceState& state) {
    const auto pending = asic.pending_irq_level();
    if (!pending.has_value()) {
        return Result<bool>::success(false);
    }
    return accept_sh4_irl_interrupt(state, *pending);
}

Sh4ReferenceBlockBoundaryHook make_dreamcast_system_irq_boundary_hook(
    DreamcastSystemAsic& asic) {
    return [&asic](Sh4ReferenceState& state) -> Result<void> {
        const auto serviced = service_dreamcast_system_irq(asic, state);
        if (!serviced) {
            return Result<void>::failure(serviced.error, serviced.detail);
        }
        return Result<void>::success();
    };
}

}
