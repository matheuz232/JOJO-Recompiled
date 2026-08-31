#pragma once
#include "core/psx_r3000a.h"
#include <cstdint>

namespace jojo {

[[nodiscard]] inline const char* psx_r3000a_exception_code_name(
    PsxR3000aExceptionCode code) noexcept {
    switch (code) {
    case PsxR3000aExceptionCode::interrupt: return "interrupt";
    case PsxR3000aExceptionCode::address_error_load: return "address_error_load";
    case PsxR3000aExceptionCode::address_error_store: return "address_error_store";
    case PsxR3000aExceptionCode::syscall: return "syscall";
    case PsxR3000aExceptionCode::breakpoint: return "breakpoint";
    case PsxR3000aExceptionCode::reserved_instruction: return "reserved_instruction";
    case PsxR3000aExceptionCode::coprocessor_unusable: return "coprocessor_unusable";
    case PsxR3000aExceptionCode::overflow: return "overflow";
    case PsxR3000aExceptionCode::none: return "none";
    }
    return "unknown";
}

[[nodiscard]] inline bool psx_r3000a_exception_in_branch_delay_slot(
    const PsxR3000aState& state) noexcept {
    return (state.cop0.cause & 0x80000000u) != 0u;
}

} // namespace jojo
