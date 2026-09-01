#pragma once

#include "core/psx_pad.h"
#include "core/psx_runtime.h"

namespace jojo {

inline void update_psx_runtime_input(PsxRuntime& runtime,
                                     const ResolvedInputFrame& input) noexcept {
    psx_sio0_set_pads(runtime.bus.sio0, make_psx_digital_pad_frame(input));
}

}
