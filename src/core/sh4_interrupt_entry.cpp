#include "core/sh4_reference_executor.h"

#include <cstdint>

namespace jojo {
namespace {

constexpr std::uint32_t kSrImaskMask = 0x000000F0u;
constexpr std::uint32_t kSrBl = 0x10000000u;
constexpr std::uint32_t kSrRb = 0x20000000u;
constexpr std::uint32_t kSrMd = 0x40000000u;

}

Result<bool> accept_sh4_irl_interrupt(Sh4ReferenceState& state,
                                      std::uint8_t level) {
    if (level == 0u || level > 15u) {
        return Result<bool>::failure(ErrorCode::invalid_argument,
                                     "SH-4 IRL interrupt level must be between 1 and 15");
    }

    const auto imask = static_cast<std::uint8_t>((state.sr & kSrImaskMask) >> 4u);
    if ((state.sr & kSrBl) != 0u || level <= imask) {
        return Result<bool>::success(false);
    }

    state.spc = state.pc;
    state.ssr = read_sh4_reference_sr(state);
    state.sgr = state.r[15];
    state.intevt = 0x00000200u +
                   static_cast<std::uint32_t>(15u - level) * 0x20u;
    write_sh4_reference_sr(state, read_sh4_reference_sr(state) | kSrMd | kSrRb | kSrBl);
    state.pc = state.vbr + 0x00000600u;
    return Result<bool>::success(true);
}

}
