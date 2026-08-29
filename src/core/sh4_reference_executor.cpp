#include "core/sh4_reference_executor.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace jojo {
namespace {

Result<void> require_register(std::uint8_t reg) {
    if (reg >= 16) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "SH-4 IR register index is out of range");
    }
    return Result<void>::success();
}

std::int32_t as_signed(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

constexpr std::uint32_t kFpscrWritableMask = 0x003FFFFFu;
constexpr std::uint32_t kFpscrFrBit = 0x00200000u;
constexpr std::uint32_t kFpscrSzBit = 0x00100000u;
constexpr std::uint32_t kFpscrPrBit = 0x00080000u;
constexpr std::uint32_t kFpscrDnBit = 0x00040000u;
constexpr std::uint32_t kFpscrRmMask = 0x00000003u;
constexpr std::uint32_t kFpscrCauseMask = 0x0003F000u;
constexpr std::uint32_t kFpscrCauseE = 0x00020000u;
constexpr std::uint32_t kFpscrCauseV = 0x00010000u;
constexpr std::uint32_t kFpscrCauseZ = 0x00008000u;
constexpr std::uint32_t kFpscrCauseO = 0x00004000u;
constexpr std::uint32_t kFpscrCauseU = 0x00002000u;
constexpr std::uint32_t kFpscrCauseI = 0x00001000u;
constexpr std::uint32_t kFpscrEnableMask = 0x00000F80u;
constexpr std::uint32_t kFpscrFlagMask = 0x0000007Cu;
constexpr std::uint32_t kSrFdBit = 0x00008000u;
constexpr std::uint32_t kSrQBit = 0x00000100u;
constexpr std::uint32_t kSrMBit = 0x00000200u;
constexpr std::uint32_t kSrSBit = 0x00000002u;
constexpr std::uint32_t kSrBlBit = 0x10000000u;
constexpr std::uint32_t kSrRbBit = 0x20000000u;
constexpr std::uint32_t kSrMdBit = 0x40000000u;

void write_fpscr(Sh4ReferenceState& state, std::uint32_t value) noexcept {
    const auto masked = value & kFpscrWritableMask;
    if (((state.fpscr ^ masked) & kFpscrFrBit) != 0u) {
        std::swap(state.fr, state.xf);
    }
    state.fpscr = masked;
}

Result<std::size_t> memory_offset(Sh4ReferenceMemoryView memory,
                                  std::uint32_t address,
                                  std::size_t width,
                                  std::uint32_t alignment) {
    if (alignment > 1u && (address % alignment) != 0u) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "reference memory access is misaligned");
    }
    if (address < memory.base_address) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "reference memory access is below the mapped range");
    }
    const auto offset = static_cast<std::size_t>(address - memory.base_address);
    if (offset > memory.bytes.size() || width > memory.bytes.size() - offset) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "reference memory access is outside the mapped range");
    }
    return Result<std::size_t>::success(offset);
}

Result<std::uint8_t> read_u8(Sh4ReferenceMemoryView memory, std::uint32_t address) {
    auto offset = memory_offset(memory, address, 1, 1);
    if (!offset) return Result<std::uint8_t>::failure(offset.error, offset.detail);
    return Result<std::uint8_t>::success(memory.bytes[offset.value]);
}

Result<std::uint16_t> read_u16(Sh4ReferenceMemoryView memory, std::uint32_t address) {
    auto offset = memory_offset(memory, address, 2, 2);
    if (!offset) return Result<std::uint16_t>::failure(offset.error, offset.detail);
    const auto value = static_cast<std::uint16_t>(memory.bytes[offset.value]) |
                       static_cast<std::uint16_t>(memory.bytes[offset.value + 1]) << 8u;
    return Result<std::uint16_t>::success(value);
}

Result<std::uint32_t> read_u32(Sh4ReferenceMemoryView memory, std::uint32_t address) {
    auto offset = memory_offset(memory, address, 4, 4);
    if (!offset) return Result<std::uint32_t>::failure(offset.error, offset.detail);
    const auto value = static_cast<std::uint32_t>(memory.bytes[offset.value]) |
                       static_cast<std::uint32_t>(memory.bytes[offset.value + 1]) << 8u |
                       static_cast<std::uint32_t>(memory.bytes[offset.value + 2]) << 16u |
                       static_cast<std::uint32_t>(memory.bytes[offset.value + 3]) << 24u;
    return Result<std::uint32_t>::success(value);
}

Result<void> write_u8(Sh4ReferenceMemoryView memory,
                      std::uint32_t address,
                      std::uint32_t value) {
    auto offset = memory_offset(memory, address, 1, 1);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    memory.bytes[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    return Result<void>::success();
}

Result<void> write_u16(Sh4ReferenceMemoryView memory,
                       std::uint32_t address,
                       std::uint32_t value) {
    auto offset = memory_offset(memory, address, 2, 2);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    memory.bytes[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    memory.bytes[offset.value + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    return Result<void>::success();
}

Result<void> write_u32(Sh4ReferenceMemoryView memory,
                       std::uint32_t address,
                       std::uint32_t value) {
    auto offset = memory_offset(memory, address, 4, 4);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    memory.bytes[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    memory.bytes[offset.value + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    memory.bytes[offset.value + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    memory.bytes[offset.value + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    return Result<void>::success();
}

std::uint32_t fpu_memory_width(const Sh4ReferenceState& state) noexcept {
    return (state.fpscr & kFpscrSzBit) != 0u ? 8u : 4u;
}

Result<void> load_fpu_memory(Sh4ReferenceState& state,
                             Sh4ReferenceMemoryView memory,
                             std::uint32_t address,
                             std::uint8_t selector) {
    auto reg = require_register(selector);
    if (!reg) return reg;

    auto first = read_u32(memory, address);
    if (!first) return Result<void>::failure(first.error, first.detail);
    if ((state.fpscr & kFpscrSzBit) == 0u) {
        state.fr[selector] = first.value;
        return Result<void>::success();
    }
    if (address > std::numeric_limits<std::uint32_t>::max() - 4u) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "reference 64-bit FPU load wraps the address space");
    }

    auto second = read_u32(memory, address + 4u);
    if (!second) return Result<void>::failure(second.error, second.detail);
    auto& bank = (selector & 1u) != 0u ? state.xf : state.fr;
    const auto base = static_cast<std::uint8_t>(selector & 0x0Eu);
    bank[base] = second.value;
    bank[base + 1u] = first.value;
    return Result<void>::success();
}

Result<void> store_fpu_memory(const Sh4ReferenceState& state,
                              Sh4ReferenceMemoryView memory,
                              std::uint32_t address,
                              std::uint8_t selector) {
    auto reg = require_register(selector);
    if (!reg) return reg;
    if ((state.fpscr & kFpscrSzBit) == 0u) {
        return write_u32(memory, address, state.fr[selector]);
    }
    if (address > std::numeric_limits<std::uint32_t>::max() - 4u) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "reference 64-bit FPU store wraps the address space");
    }

    const auto& bank = (selector & 1u) != 0u ? state.xf : state.fr;
    const auto base = static_cast<std::uint8_t>(selector & 0x0Eu);
    auto first = write_u32(memory, address, bank[base + 1u]);
    if (!first) return first;
    return write_u32(memory, address + 4u, bank[base]);
}

bool is_single_subnormal(std::uint32_t bits) noexcept {
    return (bits & 0x7F800000u) == 0u && (bits & 0x007FFFFFu) != 0u;
}

Result<float> read_single_operand(const Sh4ReferenceState& state,
                                  std::uint32_t bits) {
    if (is_single_subnormal(bits)) {
        if ((state.fpscr & kFpscrDnBit) == 0u) {
            return Result<float>::failure(
                ErrorCode::unsupported_format,
                "reference FPU arithmetic with DN=0 subnormal operands is not implemented");
        }
        return Result<float>::success(
            std::bit_cast<float>(bits & 0x80000000u));
    }

    const auto value = std::bit_cast<float>(bits);
    if (!std::isfinite(value)) {
        return Result<float>::failure(
            ErrorCode::unsupported_format,
            "reference FPU arithmetic with infinity or NaN is not implemented");
    }
    return Result<float>::success(value);
}

Result<float> read_single_compare_operand(const Sh4ReferenceState& state,
                                          std::uint32_t bits) {
    if (is_single_subnormal(bits)) {
        if ((state.fpscr & kFpscrDnBit) == 0u) {
            return Result<float>::failure(
                ErrorCode::unsupported_format,
                "reference FPU compare with DN=0 subnormal operands is not implemented");
        }
        return Result<float>::success(std::bit_cast<float>(bits & 0x80000000u));
    }
    const auto value = std::bit_cast<float>(bits);
    if (std::isnan(value)) {
        return Result<float>::failure(
            ErrorCode::unsupported_format,
            "reference FPU compare NaN exception flags are not implemented");
    }
    return Result<float>::success(value);
}

Result<std::uint32_t> calculate_single_binary(Sh4IrOp op,
                                               const Sh4ReferenceState& state,
                                               float destination,
                                               float source) {
    const auto rounding_mode = state.fpscr & kFpscrRmMask;
    if (rounding_mode > 1u) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FPU arithmetic encountered a reserved rounding mode");
    }
    if (op == Sh4IrOp::divide_single_float && source == 0.0f) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FDIV division-by-zero flags are not implemented");
    }

    double exact{};
    if (op == Sh4IrOp::add_single_float) {
        exact = static_cast<double>(destination) + static_cast<double>(source);
    } else if (op == Sh4IrOp::subtract_single_float) {
        exact = static_cast<double>(destination) - static_cast<double>(source);
    } else if (op == Sh4IrOp::multiply_single_float) {
        exact = static_cast<double>(destination) * static_cast<double>(source);
    } else {
        exact = static_cast<double>(destination) / static_cast<double>(source);
    }
    if (!std::isfinite(exact)) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FPU arithmetic overflow flags are not implemented");
    }

    auto result = static_cast<float>(exact);
    if (!std::isfinite(result)) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FPU arithmetic overflow flags are not implemented");
    }
    if (rounding_mode == 1u) {
        const auto rounded = static_cast<double>(result);
        if ((exact > 0.0 && rounded > exact) || (exact < 0.0 && rounded < exact)) {
            result = std::nextafter(result, 0.0f);
        }
    }

    auto bits = std::bit_cast<std::uint32_t>(result);
    const bool underflowed_to_zero = result == 0.0f && exact != 0.0;
    if (is_single_subnormal(bits) || underflowed_to_zero) {
        if ((state.fpscr & kFpscrDnBit) == 0u) {
            return Result<std::uint32_t>::failure(
                ErrorCode::unsupported_format,
                "reference FPU arithmetic with DN=0 subnormal results is not implemented");
        }
        bits &= 0x80000000u;
    }
    return Result<std::uint32_t>::success(bits);
}

Result<std::uint32_t> calculate_single_fmac(const Sh4ReferenceState& state,
                                             float destination,
                                             float fr0,
                                             float source) {
    const auto rounding_mode = state.fpscr & kFpscrRmMask;
    if (rounding_mode > 1u) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FMAC encountered a reserved rounding mode");
    }

    const auto exact = std::fma(static_cast<double>(fr0),
                                static_cast<double>(source),
                                static_cast<double>(destination));
    if (!std::isfinite(exact)) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FMAC overflow flags are not implemented");
    }

    auto result = std::fma(fr0, source, destination);
    if (!std::isfinite(result)) {
        return Result<std::uint32_t>::failure(
            ErrorCode::unsupported_format,
            "reference FMAC overflow flags are not implemented");
    }
    if (rounding_mode == 1u) {
        const auto rounded = static_cast<double>(result);
        if ((exact > 0.0 && rounded > exact) || (exact < 0.0 && rounded < exact)) {
            result = std::nextafter(result, 0.0f);
        }
    }

    auto bits = std::bit_cast<std::uint32_t>(result);
    const bool underflowed_to_zero = result == 0.0f && exact != 0.0;
    if (is_single_subnormal(bits) || underflowed_to_zero) {
        if ((state.fpscr & kFpscrDnBit) == 0u) {
            return Result<std::uint32_t>::failure(
                ErrorCode::unsupported_format,
                "reference FMAC with DN=0 subnormal results is not implemented");
        }
        bits &= 0x80000000u;
    }
    return Result<std::uint32_t>::success(bits);
}

bool is_load8(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::load_mem8_signed || op == Sh4IrOp::load_postinc8_signed ||
           op == Sh4IrOp::load_disp8_signed || op == Sh4IrOp::load_indexed8_signed ||
           op == Sh4IrOp::load_gbr_disp8_signed;
}

bool is_load16(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::load_mem16_signed || op == Sh4IrOp::load_postinc16_signed ||
           op == Sh4IrOp::load_disp16_signed || op == Sh4IrOp::load_indexed16_signed ||
           op == Sh4IrOp::load_gbr_disp16_signed;
}

Result<std::uint32_t> load_memory_value(Sh4IrOp op,
                                        Sh4ReferenceMemoryView memory,
                                        std::uint32_t address) {
    if (is_load8(op)) {
        auto value = read_u8(memory, address);
        if (!value) return Result<std::uint32_t>::failure(value.error, value.detail);
        const auto signed_value = std::bit_cast<std::int8_t>(value.value);
        return Result<std::uint32_t>::success(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(signed_value)));
    }
    if (is_load16(op)) {
        auto value = read_u16(memory, address);
        if (!value) return Result<std::uint32_t>::failure(value.error, value.detail);
        const auto signed_value = std::bit_cast<std::int16_t>(value.value);
        return Result<std::uint32_t>::success(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(signed_value)));
    }
    auto value = read_u32(memory, address);
    if (!value) return Result<std::uint32_t>::failure(value.error, value.detail);
    return value;
}

bool is_store8(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::store_mem8 || op == Sh4IrOp::store_predec8 ||
           op == Sh4IrOp::store_disp8 || op == Sh4IrOp::store_indexed8 ||
           op == Sh4IrOp::store_gbr_disp8;
}

bool is_store16(Sh4IrOp op) noexcept {
    return op == Sh4IrOp::store_mem16 || op == Sh4IrOp::store_predec16 ||
           op == Sh4IrOp::store_disp16 || op == Sh4IrOp::store_indexed16 ||
           op == Sh4IrOp::store_gbr_disp16;
}

Result<void> store_memory_value(Sh4IrOp op,
                                Sh4ReferenceMemoryView memory,
                                std::uint32_t address,
                                std::uint32_t value) {
    if (is_store8(op)) return write_u8(memory, address, value);
    if (is_store16(op)) return write_u16(memory, address, value);
    return write_u32(memory, address, value);
}

std::uint32_t memory_width(Sh4IrOp op) noexcept {
    if (is_store8(op) || is_load8(op)) return 1u;
    if (is_store16(op) || is_load16(op)) return 2u;
    return 4u;
}

struct Fpu32Eval {
    std::uint32_t bits{};
    std::uint32_t cause{};
};

struct Fpu64Eval {
    std::uint64_t bits{};
    std::uint32_t cause{};
};

bool is_double_subnormal(std::uint64_t bits) noexcept {
    return (bits & 0x7FF0000000000000ull) == 0ull &&
           (bits & 0x000FFFFFFFFFFFFFull) != 0ull;
}

std::uint64_t read_dr_bits(const Sh4ReferenceState& state, std::uint8_t even) noexcept {
    return (static_cast<std::uint64_t>(state.fr[even]) << 32u) |
           static_cast<std::uint64_t>(state.fr[even + 1u]);
}

double read_dr(const Sh4ReferenceState& state, std::uint8_t even) noexcept {
    return std::bit_cast<double>(read_dr_bits(state, even));
}

void write_dr(Sh4ReferenceState& state, std::uint8_t even, std::uint64_t bits) noexcept {
    state.fr[even] = static_cast<std::uint32_t>(bits >> 32u);
    state.fr[even + 1u] = static_cast<std::uint32_t>(bits);
}

float normalize_single(const Sh4ReferenceState& state, std::uint32_t bits) noexcept {
    if ((state.fpscr & kFpscrDnBit) != 0u && is_single_subnormal(bits)) {
        return std::bit_cast<float>(bits & 0x80000000u);
    }
    return std::bit_cast<float>(bits);
}

double normalize_double(const Sh4ReferenceState& state, std::uint64_t bits) noexcept {
    if ((state.fpscr & kFpscrDnBit) != 0u && is_double_subnormal(bits)) {
        return std::bit_cast<double>(bits & 0x8000000000000000ull);
    }
    return std::bit_cast<double>(bits);
}

float round_single(long double exact, std::uint32_t rounding_mode) noexcept {
    auto value = static_cast<float>(exact);
    if (rounding_mode == 1u && std::isfinite(value)) {
        const auto rounded = static_cast<long double>(value);
        if ((exact > 0.0L && rounded > exact) || (exact < 0.0L && rounded < exact)) {
            value = std::nextafter(value, 0.0f);
        }
    }
    return value;
}

double round_double(long double exact, std::uint32_t rounding_mode) noexcept {
    auto value = static_cast<double>(exact);
    if (rounding_mode == 1u && std::isfinite(value)) {
        const auto rounded = static_cast<long double>(value);
        if ((exact > 0.0L && rounded > exact) || (exact < 0.0L && rounded < exact)) {
            value = std::nextafter(value, 0.0);
        }
    }
    return value;
}

Fpu32Eval eval_single_binary(Sh4IrOp op,
                             const Sh4ReferenceState& state,
                             std::uint32_t lhs_bits,
                             std::uint32_t rhs_bits,
                             bool allow_denormal_inputs = false) noexcept {
    if (!allow_denormal_inputs && (state.fpscr & kFpscrDnBit) == 0u &&
        (is_single_subnormal(lhs_bits) || is_single_subnormal(rhs_bits))) {
        return {lhs_bits, kFpscrCauseE};
    }
    const auto lhs = normalize_single(state, lhs_bits);
    const auto rhs = normalize_single(state, rhs_bits);
    std::uint32_t cause{};
    float result{};

    const bool invalid = std::isnan(lhs) || std::isnan(rhs) ||
        ((op == Sh4IrOp::add_single_float || op == Sh4IrOp::subtract_single_float) &&
         std::isinf(lhs) && std::isinf(rhs) &&
         ((op == Sh4IrOp::add_single_float && std::signbit(lhs) != std::signbit(rhs)) ||
          (op == Sh4IrOp::subtract_single_float && std::signbit(lhs) == std::signbit(rhs)))) ||
        (op == Sh4IrOp::multiply_single_float &&
         ((lhs == 0.0f && std::isinf(rhs)) || (rhs == 0.0f && std::isinf(lhs)))) ||
        (op == Sh4IrOp::divide_single_float &&
         ((lhs == 0.0f && rhs == 0.0f) || (std::isinf(lhs) && std::isinf(rhs))));
    if (invalid) {
        cause |= kFpscrCauseV;
        result = std::numeric_limits<float>::quiet_NaN();
        return {std::bit_cast<std::uint32_t>(result), cause};
    }
    if (op == Sh4IrOp::divide_single_float && rhs == 0.0f) {
        cause |= kFpscrCauseZ;
        result = std::copysign(std::numeric_limits<float>::infinity(), lhs * rhs);
        return {std::bit_cast<std::uint32_t>(result), cause};
    }

    long double exact{};
    if (op == Sh4IrOp::add_single_float) exact = static_cast<long double>(lhs) + static_cast<long double>(rhs);
    else if (op == Sh4IrOp::subtract_single_float) exact = static_cast<long double>(lhs) - static_cast<long double>(rhs);
    else if (op == Sh4IrOp::multiply_single_float) exact = static_cast<long double>(lhs) * static_cast<long double>(rhs);
    else exact = static_cast<long double>(lhs) / static_cast<long double>(rhs);

    result = round_single(exact, state.fpscr & kFpscrRmMask);
    if (std::isfinite(lhs) && std::isfinite(rhs) && !std::isfinite(result)) {
        cause |= kFpscrCauseO | kFpscrCauseI;
    } else if (exact != 0.0L && (result == 0.0f || std::fpclassify(result) == FP_SUBNORMAL)) {
        cause |= kFpscrCauseU | kFpscrCauseI;
    } else if (std::isfinite(result) && static_cast<long double>(result) != exact) {
        cause |= kFpscrCauseI;
    }
    auto bits = std::bit_cast<std::uint32_t>(result);
    if ((state.fpscr & kFpscrDnBit) != 0u && is_single_subnormal(bits)) {
        bits &= 0x80000000u;
    }
    return {bits, cause};
}

Fpu64Eval eval_double_binary(Sh4IrOp op,
                             const Sh4ReferenceState& state,
                             std::uint64_t lhs_bits,
                             std::uint64_t rhs_bits,
                             bool allow_denormal_inputs = false) noexcept {
    if (!allow_denormal_inputs && (state.fpscr & kFpscrDnBit) == 0u &&
        (is_double_subnormal(lhs_bits) || is_double_subnormal(rhs_bits))) {
        return {lhs_bits, kFpscrCauseE};
    }
    const auto lhs = normalize_double(state, lhs_bits);
    const auto rhs = normalize_double(state, rhs_bits);
    std::uint32_t cause{};
    double result{};
    const bool invalid = std::isnan(lhs) || std::isnan(rhs) ||
        ((op == Sh4IrOp::add_single_float || op == Sh4IrOp::subtract_single_float) &&
         std::isinf(lhs) && std::isinf(rhs) &&
         ((op == Sh4IrOp::add_single_float && std::signbit(lhs) != std::signbit(rhs)) ||
          (op == Sh4IrOp::subtract_single_float && std::signbit(lhs) == std::signbit(rhs)))) ||
        (op == Sh4IrOp::multiply_single_float &&
         ((lhs == 0.0 && std::isinf(rhs)) || (rhs == 0.0 && std::isinf(lhs)))) ||
        (op == Sh4IrOp::divide_single_float &&
         ((lhs == 0.0 && rhs == 0.0) || (std::isinf(lhs) && std::isinf(rhs))));
    if (invalid) {
        cause |= kFpscrCauseV;
        result = std::numeric_limits<double>::quiet_NaN();
        return {std::bit_cast<std::uint64_t>(result), cause};
    }
    if (op == Sh4IrOp::divide_single_float && rhs == 0.0) {
        cause |= kFpscrCauseZ;
        result = std::copysign(std::numeric_limits<double>::infinity(), lhs * rhs);
        return {std::bit_cast<std::uint64_t>(result), cause};
    }
    long double exact{};
    if (op == Sh4IrOp::add_single_float) exact = static_cast<long double>(lhs) + static_cast<long double>(rhs);
    else if (op == Sh4IrOp::subtract_single_float) exact = static_cast<long double>(lhs) - static_cast<long double>(rhs);
    else if (op == Sh4IrOp::multiply_single_float) exact = static_cast<long double>(lhs) * static_cast<long double>(rhs);
    else exact = static_cast<long double>(lhs) / static_cast<long double>(rhs);
    result = round_double(exact, state.fpscr & kFpscrRmMask);
    if (std::isfinite(lhs) && std::isfinite(rhs) && !std::isfinite(result)) cause |= kFpscrCauseO | kFpscrCauseI;
    else if (exact != 0.0L && (result == 0.0 || std::fpclassify(result) == FP_SUBNORMAL)) cause |= kFpscrCauseU | kFpscrCauseI;
    else if (std::isfinite(result) && static_cast<long double>(result) != exact) cause |= kFpscrCauseI;
    auto bits = std::bit_cast<std::uint64_t>(result);
    if ((state.fpscr & kFpscrDnBit) != 0u && is_double_subnormal(bits)) bits &= 0x8000000000000000ull;
    return {bits, cause};
}

bool is_fpu_ir_op(Sh4IrOp op) noexcept {
    return (op >= Sh4IrOp::set_fr_zero && op <= Sh4IrOp::toggle_fpscr_sz) ||
           op == Sh4IrOp::set_fpul_from_reg ||
           op == Sh4IrOp::copy_fpul_to_reg ||
           op == Sh4IrOp::load_fpul_postinc32 ||
           op == Sh4IrOp::store_fpul_predec32 ||
           op == Sh4IrOp::set_fpscr_from_reg ||
           op == Sh4IrOp::copy_fpscr_to_reg ||
           op == Sh4IrOp::load_fpscr_postinc32 ||
           op == Sh4IrOp::store_fpscr_predec32;
}

struct PendingTransfer {
    std::uint32_t target{};
    std::optional<bool> condition;
    bool immediate{};
};

void enter_general_exception(const Sh4IrInstruction& instruction,
                             Sh4ReferenceState& state,
                             std::optional<PendingTransfer>& pending,
                             std::uint32_t event_code) {
    state.spc = instruction.in_delay_slot
        ? instruction.source_address - 2u
        : instruction.source_address;
    state.ssr = read_sh4_reference_sr(state);
    state.sgr = state.r[15];
    state.expevt = event_code;
    write_sh4_reference_sr(state, state.ssr | kSrMdBit | kSrRbBit | kSrBlBit);
    pending = PendingTransfer{state.vbr + 0x100u, std::nullopt, true};
}

bool apply_fpu_cause(const Sh4IrInstruction& instruction,
                     Sh4ReferenceState& state,
                     std::optional<PendingTransfer>& pending,
                     std::uint32_t cause) {
    cause &= (kFpscrCauseE | kFpscrCauseV | kFpscrCauseZ | kFpscrCauseO | kFpscrCauseU | kFpscrCauseI);
    state.fpscr = (state.fpscr & ~kFpscrCauseMask) | cause;
    const auto sticky = cause & (kFpscrCauseV | kFpscrCauseZ | kFpscrCauseO | kFpscrCauseU | kFpscrCauseI);
    state.fpscr |= (sticky >> 10u) & kFpscrFlagMask;
    const auto enabled = (sticky >> 5u) & kFpscrEnableMask;
    if ((cause & kFpscrCauseE) != 0u || (state.fpscr & enabled) != 0u) {
        enter_general_exception(instruction, state, pending, 0x120u);
        return false;
    }
    return true;
}

bool require_fpu_pair(const Sh4IrInstruction& instruction,
                      Sh4ReferenceState& state,
                      std::optional<PendingTransfer>& pending,
                      std::uint8_t selector) {
    if ((selector & 1u) == 0u && selector < 15u) return true;
    enter_general_exception(instruction, state, pending,
                            instruction.in_delay_slot ? 0x1A0u : 0x180u);
    return false;
}

bool require_privileged(const Sh4IrInstruction& instruction,
                        Sh4ReferenceState& state,
                        std::optional<PendingTransfer>& pending) {
    if ((state.sr & kSrMdBit) != 0u) return true;
    enter_general_exception(instruction, state, pending,
                            instruction.in_delay_slot ? 0x1A0u : 0x180u);
    return false;
}

std::uint32_t control_value(const Sh4ReferenceState& state, std::int32_t selector) {
    switch (selector) {
        case 0: return read_sh4_reference_sr(state);
        case 1: return state.vbr;
        case 2: return state.ssr;
        case 3: return state.spc;
        case 4: return state.sgr;
        case 5: return state.dbr;
        default: return 0u;
    }
}

void set_control_value(Sh4ReferenceState& state, std::int32_t selector, std::uint32_t value) {
    switch (selector) {
        case 0: write_sh4_reference_sr(state, value); break;
        case 1: state.vbr = value; break;
        case 2: state.ssr = value; break;
        case 3: state.spc = value; break;
        case 4: state.sgr = value; break;
        case 5: state.dbr = value; break;
        default: break;
    }
}

Result<void> execute_op(const Sh4IrInstruction& instruction,
                        Sh4ReferenceState& state,
                        Sh4ReferenceMemoryView memory,
                        std::optional<PendingTransfer>& pending) {
    if (is_fpu_ir_op(instruction.op) && (state.sr & kSrFdBit) != 0u) {
        enter_general_exception(instruction, state, pending,
                                instruction.in_delay_slot ? 0x820u : 0x800u);
        return Result<void>::success();
    }
    switch (instruction.op) {
        case Sh4IrOp::nop:
            return Result<void>::success();
        case Sh4IrOp::clear_s:
            state.sr &= ~kSrSBit;
            return Result<void>::success();
        case Sh4IrOp::set_s:
            state.sr |= kSrSBit;
            return Result<void>::success();
        case Sh4IrOp::ldtlb_event:
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            state.last_system_event = Sh4ReferenceSystemEvent::ldtlb;
            state.system_event_address = 0u;
            return Result<void>::success();
        case Sh4IrOp::sleep_cpu:
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            state.sleeping = true;
            state.last_system_event = Sh4ReferenceSystemEvent::sleep;
            state.system_event_address = 0u;
            return Result<void>::success();
        case Sh4IrOp::clear_t:
            state.t = false;
            return Result<void>::success();
        case Sh4IrOp::set_t:
            state.t = true;
            return Result<void>::success();
        case Sh4IrOp::move_t: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.r[instruction.dst_reg] = state.t ? 1u : 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::set_imm: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.r[instruction.dst_reg] = static_cast<std::uint32_t>(instruction.imm);
            return Result<void>::success();
        }
        case Sh4IrOp::test_and_set_byte: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            const auto address = state.r[instruction.dst_reg];
            auto value = read_u8(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.t = value.value == 0u;
            return write_u8(memory, address, static_cast<std::uint8_t>(value.value | 0x80u));
        }
        case Sh4IrOp::trap_imm:
            if (instruction.in_delay_slot) {
                enter_general_exception(instruction, state, pending, 0x1A0u);
                return Result<void>::success();
            }
            state.spc = instruction.source_address + 2u;
            state.ssr = read_sh4_reference_sr(state);
            state.sgr = state.r[15];
            state.tra = static_cast<std::uint32_t>(instruction.imm & 0xFF) << 2u;
            state.expevt = 0x160u;
            write_sh4_reference_sr(state, state.ssr | kSrMdBit | kSrRbBit | kSrBlBit);
            pending = PendingTransfer{state.vbr + 0x100u, std::nullopt, true};
            return Result<void>::success();
        case Sh4IrOp::movca_long: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            const auto address = state.r[instruction.dst_reg];
            auto stored = write_u32(memory, address, state.r[0]);
            if (!stored) return stored;
            state.last_system_event = Sh4ReferenceSystemEvent::movca_l;
            state.system_event_address = address;
            return Result<void>::success();
        }
        case Sh4IrOp::ocbi_event:
        case Sh4IrOp::ocbp_event:
        case Sh4IrOp::ocbwb_event:
        case Sh4IrOp::pref_event: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.system_event_address = state.r[instruction.dst_reg];
            if (instruction.op == Sh4IrOp::ocbi_event) state.last_system_event = Sh4ReferenceSystemEvent::ocbi;
            else if (instruction.op == Sh4IrOp::ocbp_event) state.last_system_event = Sh4ReferenceSystemEvent::ocbp;
            else if (instruction.op == Sh4IrOp::ocbwb_event) state.last_system_event = Sh4ReferenceSystemEvent::ocbwb;
            else state.last_system_event = Sh4ReferenceSystemEvent::pref;
            return Result<void>::success();
        }
        case Sh4IrOp::set_fr_zero:
        case Sh4IrOp::set_fr_one: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.fr[instruction.dst_reg] = instruction.op == Sh4IrOp::set_fr_zero ? 0u : 0x3F800000u;
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fr_to_fpul: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.fpul = state.fr[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fpul_to_fr: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.fr[instruction.dst_reg] = state.fpul;
            return Result<void>::success();
        }
        case Sh4IrOp::convert_fpul_to_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto integer = std::bit_cast<std::int32_t>(state.fpul);
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg)) return Result<void>::success();
                const auto result = static_cast<double>(integer);
                if (!apply_fpu_cause(instruction, state, pending, 0u)) return Result<void>::success();
                write_dr(state, instruction.dst_reg, std::bit_cast<std::uint64_t>(result));
            } else {
                const auto result = static_cast<float>(integer);
                const auto cause = static_cast<std::int32_t>(result) == integer ? 0u : kFpscrCauseI;
                if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
                state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(result);
            }
            return Result<void>::success();
        }
        case Sh4IrOp::truncate_float_to_fpul: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            long double value{};
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.src_reg)) return Result<void>::success();
                value = static_cast<long double>(normalize_double(state, read_dr_bits(state, instruction.src_reg)));
            } else {
                value = static_cast<long double>(normalize_single(state, state.fr[instruction.src_reg]));
            }
            const auto truncated = std::trunc(value);
            constexpr long double min_value = static_cast<long double>(std::numeric_limits<std::int32_t>::min());
            constexpr long double max_value = static_cast<long double>(std::numeric_limits<std::int32_t>::max());
            std::uint32_t cause{};
            std::uint32_t result = 0x80000000u;
            if (!std::isfinite(value) || truncated < min_value || truncated > max_value) {
                cause = kFpscrCauseV;
            } else {
                result = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(truncated));
                if (truncated != value) cause |= kFpscrCauseI;
            }
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            state.fpul = result;
            return Result<void>::success();
        }
        case Sh4IrOp::negate_single_float:
        case Sh4IrOp::absolute_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg)) return Result<void>::success();
            }
            if (instruction.op == Sh4IrOp::negate_single_float) state.fr[instruction.dst_reg] ^= 0x80000000u;
            else state.fr[instruction.dst_reg] &= 0x7FFFFFFFu;
            return Result<void>::success();
        }
        case Sh4IrOp::sqrt_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            std::uint32_t cause{};
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg)) return Result<void>::success();
                const auto operand_bits = read_dr_bits(state, instruction.dst_reg);
                if ((state.fpscr & kFpscrDnBit) == 0u && is_double_subnormal(operand_bits)) {
                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                    return Result<void>::success();
                }
                const auto value = normalize_double(state, operand_bits);
                double result{};
                if (std::isnan(value) || (value < 0.0 && value != 0.0)) {
                    cause = kFpscrCauseV;
                    result = std::numeric_limits<double>::quiet_NaN();
                } else {
                    const auto exact = std::sqrt(static_cast<long double>(value));
                    result = round_double(exact, state.fpscr & kFpscrRmMask);
                    if (std::isfinite(result) && static_cast<long double>(result) != exact) cause |= kFpscrCauseI;
                }
                if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
                write_dr(state, instruction.dst_reg, std::bit_cast<std::uint64_t>(result));
            } else {
                const auto operand_bits = state.fr[instruction.dst_reg];
                if ((state.fpscr & kFpscrDnBit) == 0u && is_single_subnormal(operand_bits)) {
                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                    return Result<void>::success();
                }
                const auto value = normalize_single(state, operand_bits);
                float result{};
                if (std::isnan(value) || (value < 0.0f && value != 0.0f)) {
                    cause = kFpscrCauseV;
                    result = std::numeric_limits<float>::quiet_NaN();
                } else {
                    const auto exact = std::sqrt(static_cast<long double>(value));
                    result = round_single(exact, state.fpscr & kFpscrRmMask);
                    if (std::isfinite(result) && static_cast<long double>(result) != exact) cause |= kFpscrCauseI;
                }
                if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
                state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(result);
            }
            return Result<void>::success();
        }
        case Sh4IrOp::multiply_add_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            if ((state.fpscr & kFpscrDnBit) == 0u &&
                (is_single_subnormal(state.fr[0]) ||
                 is_single_subnormal(state.fr[instruction.src_reg]) ||
                 is_single_subnormal(state.fr[instruction.dst_reg]))) {
                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                return Result<void>::success();
            }
            const auto a = normalize_single(state, state.fr[0]);
            const auto b = normalize_single(state, state.fr[instruction.src_reg]);
            const auto c = normalize_single(state, state.fr[instruction.dst_reg]);
            std::uint32_t cause{};
            float result{};
            if (std::isnan(a) || std::isnan(b) || std::isnan(c) ||
                ((a == 0.0f && std::isinf(b)) || (b == 0.0f && std::isinf(a)))) {
                cause = kFpscrCauseV;
                result = std::numeric_limits<float>::quiet_NaN();
            } else {
                const auto exact = std::fma(static_cast<long double>(a), static_cast<long double>(b), static_cast<long double>(c));
                result = round_single(exact, state.fpscr & kFpscrRmMask);
                if (std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && !std::isfinite(result)) cause |= kFpscrCauseO | kFpscrCauseI;
                else if (exact != 0.0L && (result == 0.0f || std::fpclassify(result) == FP_SUBNORMAL)) cause |= kFpscrCauseU | kFpscrCauseI;
                else if (std::isfinite(result) && static_cast<long double>(result) != exact) cause |= kFpscrCauseI;
            }
            auto bits = std::bit_cast<std::uint32_t>(result);
            if ((state.fpscr & kFpscrDnBit) != 0u && is_single_subnormal(bits)) bits &= 0x80000000u;
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            state.fr[instruction.dst_reg] = bits;
            return Result<void>::success();
        }
        case Sh4IrOp::add_single_float:
        case Sh4IrOp::subtract_single_float:
        case Sh4IrOp::multiply_single_float:
        case Sh4IrOp::divide_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg) ||
                    !require_fpu_pair(instruction, state, pending, instruction.src_reg)) return Result<void>::success();
                const auto eval = eval_double_binary(instruction.op, state,
                                                     read_dr_bits(state, instruction.dst_reg),
                                                     read_dr_bits(state, instruction.src_reg));
                if (!apply_fpu_cause(instruction, state, pending, eval.cause)) return Result<void>::success();
                write_dr(state, instruction.dst_reg, eval.bits);
            } else {
                const auto eval = eval_single_binary(instruction.op, state,
                                                     state.fr[instruction.dst_reg],
                                                     state.fr[instruction.src_reg]);
                if (!apply_fpu_cause(instruction, state, pending, eval.cause)) return Result<void>::success();
                state.fr[instruction.dst_reg] = eval.bits;
            }
            return Result<void>::success();
        }
        case Sh4IrOp::compare_single_float_eq:
        case Sh4IrOp::compare_single_float_gt: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            bool value{};
            std::uint32_t cause{};
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg) ||
                    !require_fpu_pair(instruction, state, pending, instruction.src_reg)) return Result<void>::success();
                const auto lhs_bits = read_dr_bits(state, instruction.dst_reg);
                const auto rhs_bits = read_dr_bits(state, instruction.src_reg);
                if ((state.fpscr & kFpscrDnBit) == 0u &&
                    (is_double_subnormal(lhs_bits) || is_double_subnormal(rhs_bits))) {
                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                    return Result<void>::success();
                }
                const auto lhs = normalize_double(state, lhs_bits);
                const auto rhs = normalize_double(state, rhs_bits);
                if (std::isnan(lhs) || std::isnan(rhs)) cause = kFpscrCauseV;
                else value = instruction.op == Sh4IrOp::compare_single_float_eq ? lhs == rhs : lhs > rhs;
            } else {
                const auto lhs_bits = state.fr[instruction.dst_reg];
                const auto rhs_bits = state.fr[instruction.src_reg];
                if ((state.fpscr & kFpscrDnBit) == 0u &&
                    (is_single_subnormal(lhs_bits) || is_single_subnormal(rhs_bits))) {
                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                    return Result<void>::success();
                }
                const auto lhs = normalize_single(state, lhs_bits);
                const auto rhs = normalize_single(state, rhs_bits);
                if (std::isnan(lhs) || std::isnan(rhs)) cause = kFpscrCauseV;
                else value = instruction.op == Sh4IrOp::compare_single_float_eq ? lhs == rhs : lhs > rhs;
            }
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            state.t = value;
            return Result<void>::success();
        }
        case Sh4IrOp::convert_single_to_double: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if ((state.fpscr & kFpscrPrBit) == 0u || (state.fpscr & kFpscrSzBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg)) return Result<void>::success();
            if ((state.fpscr & kFpscrDnBit) == 0u && is_single_subnormal(state.fpul)) {
                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                return Result<void>::success();
            }
            const auto value = normalize_single(state, state.fpul);
            std::uint32_t cause = std::isnan(value) ? kFpscrCauseV : 0u;
            const auto result = static_cast<double>(value);
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            write_dr(state, instruction.dst_reg, std::bit_cast<std::uint64_t>(result));
            return Result<void>::success();
        }
        case Sh4IrOp::convert_double_to_single: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if ((state.fpscr & kFpscrPrBit) == 0u || (state.fpscr & kFpscrSzBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            if (!require_fpu_pair(instruction, state, pending, instruction.src_reg)) return Result<void>::success();
            const auto operand_bits = read_dr_bits(state, instruction.src_reg);
            if ((state.fpscr & kFpscrDnBit) == 0u && is_double_subnormal(operand_bits)) {
                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);
                return Result<void>::success();
            }
            const auto value = normalize_double(state, operand_bits);
            std::uint32_t cause{};
            float result{};
            if (std::isnan(value)) {
                cause = kFpscrCauseV;
                result = std::numeric_limits<float>::quiet_NaN();
            } else {
                result = round_single(static_cast<long double>(value), state.fpscr & kFpscrRmMask);
                if (std::isfinite(value) && !std::isfinite(result)) cause |= kFpscrCauseO | kFpscrCauseI;
                else if (value != 0.0 && (result == 0.0f || std::fpclassify(result) == FP_SUBNORMAL)) cause |= kFpscrCauseU | kFpscrCauseI;
                else if (static_cast<double>(result) != value) cause |= kFpscrCauseI;
            }
            auto bits = std::bit_cast<std::uint32_t>(result);
            if ((state.fpscr & kFpscrDnBit) != 0u && is_single_subnormal(bits)) bits &= 0x80000000u;
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            state.fpul = bits;
            return Result<void>::success();
        }
        case Sh4IrOp::inner_product_single: {
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            float accumulator = 0.0f;
            std::uint32_t cause{};
            for (std::uint8_t index = 0; index < 4u; ++index) {
                const auto mul = eval_single_binary(Sh4IrOp::multiply_single_float, state,
                                                    state.fr[instruction.dst_reg + index],
                                                    state.fr[instruction.src_reg + index], true);
                cause |= mul.cause;
                const auto add = eval_single_binary(Sh4IrOp::add_single_float, state,
                                                    std::bit_cast<std::uint32_t>(accumulator), mul.bits, true);
                cause |= add.cause;
                accumulator = std::bit_cast<float>(add.bits);
            }
            cause |= kFpscrCauseI;
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            state.fr[instruction.dst_reg + 3u] = std::bit_cast<std::uint32_t>(accumulator);
            return Result<void>::success();
        }
        case Sh4IrOp::matrix_transform_single: {
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            std::array<std::uint32_t, 4> source{};
            std::array<std::uint32_t, 4> output{};
            for (std::uint8_t i = 0; i < 4u; ++i) source[i] = state.fr[instruction.dst_reg + i];
            std::uint32_t cause{};
            for (std::uint8_t row = 0; row < 4u; ++row) {
                std::uint32_t acc = std::bit_cast<std::uint32_t>(0.0f);
                for (std::uint8_t col = 0; col < 4u; ++col) {
                    const auto mul = eval_single_binary(Sh4IrOp::multiply_single_float, state,
                                                        state.xf[col * 4u + row], source[col], true);
                    cause |= mul.cause;
                    const auto add = eval_single_binary(Sh4IrOp::add_single_float, state, acc, mul.bits, true);
                    cause |= add.cause;
                    acc = add.bits;
                }
                output[row] = acc;
            }
            cause |= kFpscrCauseI;
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            for (std::uint8_t i = 0; i < 4u; ++i) state.fr[instruction.dst_reg + i] = output[i];
            return Result<void>::success();
        }
        case Sh4IrOp::sine_cosine_single: {
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            const auto phase = static_cast<std::uint16_t>(state.fpul & 0xFFFFu);
            float sine{};
            float cosine{};
            if (phase == 0x0000u) { sine = 0.0f; cosine = 1.0f; }
            else if (phase == 0x4000u) { sine = 1.0f; cosine = 0.0f; }
            else if (phase == 0x8000u) { sine = 0.0f; cosine = -1.0f; }
            else if (phase == 0xC000u) { sine = -1.0f; cosine = 0.0f; }
            else {
                constexpr long double tau = 6.283185307179586476925286766559005768L;
                const auto angle = static_cast<long double>(phase) * tau / 65536.0L;
                sine = static_cast<float>(std::sin(angle));
                cosine = static_cast<float>(std::cos(angle));
            }
            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(sine);
            state.fr[instruction.dst_reg + 1u] = std::bit_cast<std::uint32_t>(cosine);
            return Result<void>::success();
        }
        case Sh4IrOp::reciprocal_sqrt_single: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            const auto value = normalize_single(state, state.fr[instruction.dst_reg]);
            std::uint32_t cause{};
            float result{};
            if (std::isnan(value) || value < 0.0f) {
                cause = kFpscrCauseV;
                result = std::numeric_limits<float>::quiet_NaN();
            } else if (value == 0.0f) {
                cause = kFpscrCauseZ;
                result = std::copysign(std::numeric_limits<float>::infinity(), value);
            } else {
                result = 1.0f / std::sqrt(value);
                cause = kFpscrCauseI;
            }
            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();
            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(result);
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fpu_registers: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if ((state.fpscr & kFpscrSzBit) == 0u) {
                state.fr[instruction.dst_reg] = state.fr[instruction.src_reg];
                return Result<void>::success();
            }
            auto& source_bank = (instruction.src_reg & 1u) != 0u ? state.xf : state.fr;
            auto& destination_bank = (instruction.dst_reg & 1u) != 0u ? state.xf : state.fr;
            const auto source_base = static_cast<std::uint8_t>(instruction.src_reg & 0x0Eu);
            const auto destination_base = static_cast<std::uint8_t>(instruction.dst_reg & 0x0Eu);
            const auto first = source_bank[source_base];
            const auto second = source_bank[source_base + 1u];
            destination_bank[destination_base] = first;
            destination_bank[destination_base + 1u] = second;
            return Result<void>::success();
        }
        case Sh4IrOp::store_fpu_memory: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            return store_fpu_memory(state, memory, state.r[instruction.dst_reg], instruction.src_reg);
        }
        case Sh4IrOp::load_fpu_memory: {
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            return load_fpu_memory(state, memory, state.r[instruction.src_reg], instruction.dst_reg);
        }
        case Sh4IrOp::load_fpu_postincrement: {
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg];
            auto loaded = load_fpu_memory(state, memory, address, instruction.dst_reg);
            if (!loaded) return loaded;
            state.r[instruction.src_reg] += fpu_memory_width(state);
            return Result<void>::success();
        }
        case Sh4IrOp::store_fpu_predecrement: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.dst_reg] - fpu_memory_width(state);
            auto stored = store_fpu_memory(state, memory, address, instruction.src_reg);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::load_fpu_indexed: {
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[0] + state.r[instruction.src_reg];
            return load_fpu_memory(state, memory, address, instruction.dst_reg);
        }
        case Sh4IrOp::store_fpu_indexed: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[0] + state.r[instruction.dst_reg];
            return store_fpu_memory(state, memory, address, instruction.src_reg);
        }
        case Sh4IrOp::toggle_fpscr_fr:
            write_fpscr(state, state.fpscr ^ kFpscrFrBit);
            return Result<void>::success();
        case Sh4IrOp::toggle_fpscr_sz:
            write_fpscr(state, state.fpscr ^ kFpscrSzBit);
            return Result<void>::success();
        case Sh4IrOp::add_imm: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.r[instruction.dst_reg] += static_cast<std::uint32_t>(instruction.imm);
            return Result<void>::success();
        }
        case Sh4IrOp::copy_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] = state.r[instruction.src_reg];
            return Result<void>::success();
        }

        case Sh4IrOp::store_mem8:
        case Sh4IrOp::store_mem16:
        case Sh4IrOp::store_mem32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            return store_memory_value(instruction.op, memory,
                                      state.r[instruction.dst_reg],
                                      state.r[instruction.src_reg]);
        }
        case Sh4IrOp::load_mem8_signed:
        case Sh4IrOp::load_mem16_signed:
        case Sh4IrOp::load_mem32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg];
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_predec8:
        case Sh4IrOp::store_predec16:
        case Sh4IrOp::store_predec32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            const auto source_value = state.r[instruction.src_reg];
            const auto address = state.r[instruction.dst_reg] - memory_width(instruction.op);
            auto stored = store_memory_value(instruction.op, memory, address, source_value);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::load_postinc8_signed:
        case Sh4IrOp::load_postinc16_signed:
        case Sh4IrOp::load_postinc32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg];
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            if (instruction.dst_reg != instruction.src_reg) {
                state.r[instruction.src_reg] += memory_width(instruction.op);
            }
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_disp8:
        case Sh4IrOp::store_disp16:
        case Sh4IrOp::store_disp32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            const auto address = state.r[instruction.dst_reg] + static_cast<std::uint32_t>(instruction.imm);
            return store_memory_value(instruction.op, memory, address, state.r[instruction.src_reg]);
        }
        case Sh4IrOp::load_disp8_signed:
        case Sh4IrOp::load_disp16_signed:
        case Sh4IrOp::load_disp32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg] + static_cast<std::uint32_t>(instruction.imm);
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_indexed8:
        case Sh4IrOp::store_indexed16:
        case Sh4IrOp::store_indexed32: {
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            auto value_reg = require_register(instruction.src_reg);
            if (!value_reg) return value_reg;
            const auto address = state.r[0] + state.r[instruction.dst_reg];
            return store_memory_value(instruction.op, memory, address, state.r[instruction.src_reg]);
        }
        case Sh4IrOp::load_indexed8_signed:
        case Sh4IrOp::load_indexed16_signed:
        case Sh4IrOp::load_indexed32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[0] + state.r[instruction.src_reg];
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_gbr_disp8:
        case Sh4IrOp::store_gbr_disp16:
        case Sh4IrOp::store_gbr_disp32: {
            const auto address = state.gbr + static_cast<std::uint32_t>(instruction.imm);
            return store_memory_value(instruction.op, memory, address, state.r[0]);
        }
        case Sh4IrOp::load_gbr_disp8_signed:
        case Sh4IrOp::load_gbr_disp16_signed:
        case Sh4IrOp::load_gbr_disp32: {
            const auto address = state.gbr + static_cast<std::uint32_t>(instruction.imm);
            auto value = load_memory_value(instruction.op, memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[0] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::set_gbr_from_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.gbr = state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::copy_gbr_to_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.r[instruction.dst_reg] = state.gbr;
            return Result<void>::success();
        }
        case Sh4IrOp::load_gbr_postinc32: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto address = state.r[instruction.src_reg];
            auto value = read_u32(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.gbr = value.value;
            state.r[instruction.src_reg] += 4u;
            return Result<void>::success();
        }
        case Sh4IrOp::store_gbr_predec32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto address = state.r[instruction.dst_reg] - 4u;
            auto stored = write_u32(memory, address, state.gbr);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }

        case Sh4IrOp::set_control_from_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            if (instruction.imm == 0 && instruction.in_delay_slot) {
                enter_general_exception(instruction, state, pending, 0x1A0u);
                return Result<void>::success();
            }
            const auto value = state.r[instruction.src_reg];
            set_control_value(state, instruction.imm, value);
            return Result<void>::success();
        }
        case Sh4IrOp::copy_control_to_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            state.r[instruction.dst_reg] = control_value(state, instruction.imm);
            return Result<void>::success();
        }
        case Sh4IrOp::load_control_postinc32: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            if (instruction.imm == 0 && instruction.in_delay_slot) {
                enter_general_exception(instruction, state, pending, 0x1A0u);
                return Result<void>::success();
            }
            const auto address = state.r[instruction.src_reg];
            auto value = read_u32(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.src_reg] = address + 4u;
            set_control_value(state, instruction.imm, value.value);
            return Result<void>::success();
        }
        case Sh4IrOp::store_control_predec32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            const auto address = state.r[instruction.dst_reg] - 4u;
            auto stored = write_u32(memory, address, control_value(state, instruction.imm));
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::set_bank_from_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (instruction.dst_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, "SH-4 bank index is out of range");
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            state.r_bank[instruction.dst_reg] = state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::copy_bank_to_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if (instruction.src_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, "SH-4 bank index is out of range");
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            state.r[instruction.dst_reg] = state.r_bank[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::load_bank_postinc32: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (instruction.dst_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, "SH-4 bank index is out of range");
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            const auto address = state.r[instruction.src_reg];
            auto value = read_u32(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.src_reg] = address + 4u;
            state.r_bank[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::store_bank_predec32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if (instruction.src_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, "SH-4 bank index is out of range");
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            const auto address = state.r[instruction.dst_reg] - 4u;
            auto stored = write_u32(memory, address, state.r_bank[instruction.src_reg]);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::set_mach_from_reg:
        case Sh4IrOp::set_macl_from_reg:
        case Sh4IrOp::set_pr_from_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (instruction.op == Sh4IrOp::set_mach_from_reg) state.mach = state.r[instruction.src_reg];
            else if (instruction.op == Sh4IrOp::set_macl_from_reg) state.macl = state.r[instruction.src_reg];
            else state.pr = state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::copy_mach_to_reg:
        case Sh4IrOp::copy_macl_to_reg:
        case Sh4IrOp::copy_pr_to_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if (instruction.op == Sh4IrOp::copy_mach_to_reg) state.r[instruction.dst_reg] = state.mach;
            else if (instruction.op == Sh4IrOp::copy_macl_to_reg) state.r[instruction.dst_reg] = state.macl;
            else state.r[instruction.dst_reg] = state.pr;
            return Result<void>::success();
        }
        case Sh4IrOp::load_mach_postinc32:
        case Sh4IrOp::load_macl_postinc32:
        case Sh4IrOp::load_pr_postinc32: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto address = state.r[instruction.src_reg];
            auto value = read_u32(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            if (instruction.op == Sh4IrOp::load_mach_postinc32) state.mach = value.value;
            else if (instruction.op == Sh4IrOp::load_macl_postinc32) state.macl = value.value;
            else state.pr = value.value;
            state.r[instruction.src_reg] += 4u;
            return Result<void>::success();
        }
        case Sh4IrOp::store_mach_predec32:
        case Sh4IrOp::store_macl_predec32:
        case Sh4IrOp::store_pr_predec32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto address = state.r[instruction.dst_reg] - 4u;
            std::uint32_t value{};
            if (instruction.op == Sh4IrOp::store_mach_predec32) value = state.mach;
            else if (instruction.op == Sh4IrOp::store_macl_predec32) value = state.macl;
            else value = state.pr;
            auto stored = write_u32(memory, address, value);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::set_fpul_from_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.fpul = state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fpul_to_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.r[instruction.dst_reg] = state.fpul;
            return Result<void>::success();
        }
        case Sh4IrOp::load_fpul_postinc32: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto address = state.r[instruction.src_reg];
            auto value = read_u32(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.fpul = value.value;
            state.r[instruction.src_reg] += 4u;
            return Result<void>::success();
        }
        case Sh4IrOp::store_fpul_predec32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto address = state.r[instruction.dst_reg] - 4u;
            auto stored = write_u32(memory, address, state.fpul);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::set_fpscr_from_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            write_fpscr(state, state.r[instruction.src_reg]);
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fpscr_to_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.r[instruction.dst_reg] = state.fpscr;
            return Result<void>::success();
        }
        case Sh4IrOp::load_fpscr_postinc32: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto address = state.r[instruction.src_reg];
            auto value = read_u32(memory, address);
            if (!value) return Result<void>::failure(value.error, value.detail);
            write_fpscr(state, value.value);
            state.r[instruction.src_reg] += 4u;
            return Result<void>::success();
        }
        case Sh4IrOp::store_fpscr_predec32: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto address = state.r[instruction.dst_reg] - 4u;
            auto stored = write_u32(memory, address, state.fpscr);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }

        case Sh4IrOp::clear_mac:
            state.mach = 0u;
            state.macl = 0u;
            return Result<void>::success();
        case Sh4IrOp::multiply_accumulate_long: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto dst_address = state.r[instruction.dst_reg];
            const auto src_address = state.r[instruction.src_reg];
            auto lhs_raw = read_u32(memory, src_address);
            if (!lhs_raw) return Result<void>::failure(lhs_raw.error, lhs_raw.detail);
            auto rhs_raw = read_u32(memory, dst_address);
            if (!rhs_raw) return Result<void>::failure(rhs_raw.error, rhs_raw.detail);
            const auto lhs = std::bit_cast<std::int32_t>(lhs_raw.value);
            const auto rhs = std::bit_cast<std::int32_t>(rhs_raw.value);
            const auto product = static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs);
            const std::uint64_t combined = (static_cast<std::uint64_t>(state.mach) << 32u) | state.macl;
            std::uint64_t result_bits{};
            if ((state.sr & kSrSBit) == 0u) {
                result_bits = combined + static_cast<std::uint64_t>(product);
            } else {
                constexpr std::int64_t min48 = -(static_cast<std::int64_t>(1) << 47u);
                constexpr std::int64_t max48 = (static_cast<std::int64_t>(1) << 47u) - 1;
                const auto accumulator = std::bit_cast<std::int64_t>(combined);
                std::int64_t sum{};
                if (product > 0 && accumulator > std::numeric_limits<std::int64_t>::max() - product) sum = max48;
                else if (product < 0 && accumulator < std::numeric_limits<std::int64_t>::min() - product) sum = min48;
                else {
                    sum = accumulator + product;
                    if (sum < min48) sum = min48;
                    if (sum > max48) sum = max48;
                }
                result_bits = std::bit_cast<std::uint64_t>(sum);
            }
            state.macl = static_cast<std::uint32_t>(result_bits);
            state.mach = static_cast<std::uint32_t>(result_bits >> 32u);
            if (instruction.dst_reg == instruction.src_reg) state.r[instruction.dst_reg] = dst_address + 8u;
            else {
                state.r[instruction.dst_reg] = dst_address + 4u;
                state.r[instruction.src_reg] = src_address + 4u;
            }
            return Result<void>::success();
        }
        case Sh4IrOp::multiply_accumulate_word: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto dst_address = state.r[instruction.dst_reg];
            const auto src_address = state.r[instruction.src_reg];
            auto lhs_raw = read_u16(memory, src_address);
            if (!lhs_raw) return Result<void>::failure(lhs_raw.error, lhs_raw.detail);
            auto rhs_raw = read_u16(memory, dst_address);
            if (!rhs_raw) return Result<void>::failure(rhs_raw.error, rhs_raw.detail);
            const auto lhs = std::bit_cast<std::int16_t>(lhs_raw.value);
            const auto rhs = std::bit_cast<std::int16_t>(rhs_raw.value);
            const auto product = static_cast<std::int32_t>(lhs) * static_cast<std::int32_t>(rhs);
            if ((state.sr & kSrSBit) != 0u) {
                const auto accumulator = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(state.macl));
                const auto sum = accumulator + static_cast<std::int64_t>(product);
                if (sum > std::numeric_limits<std::int32_t>::max()) { state.macl = 0x7FFFFFFFu; state.mach = 1u; }
                else if (sum < std::numeric_limits<std::int32_t>::min()) { state.macl = 0x80000000u; state.mach = 1u; }
                else state.macl = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(sum));
            } else {
                const std::uint64_t combined = (static_cast<std::uint64_t>(state.mach) << 32u) | state.macl;
                const auto result = combined + static_cast<std::uint64_t>(static_cast<std::int64_t>(product));
                state.macl = static_cast<std::uint32_t>(result);
                state.mach = static_cast<std::uint32_t>(result >> 32u);
            }
            if (instruction.dst_reg == instruction.src_reg) state.r[instruction.dst_reg] = dst_address + 4u;
            else {
                state.r[instruction.dst_reg] = dst_address + 2u;
                state.r[instruction.src_reg] = src_address + 2u;
            }
            return Result<void>::success();
        }
        case Sh4IrOp::multiply_low32:
        case Sh4IrOp::multiply_signed_word:
        case Sh4IrOp::multiply_unsigned_word:
        case Sh4IrOp::multiply_signed_long:
        case Sh4IrOp::multiply_unsigned_long: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;

            const auto lhs = state.r[instruction.dst_reg];
            const auto rhs = state.r[instruction.src_reg];
            if (instruction.op == Sh4IrOp::multiply_low32) {
                const auto product = static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs);
                state.macl = static_cast<std::uint32_t>(product);
            } else if (instruction.op == Sh4IrOp::multiply_signed_word) {
                const auto lhs16 = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(lhs & 0xFFFFu));
                const auto rhs16 = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(rhs & 0xFFFFu));
                const auto product = static_cast<std::int32_t>(lhs16) * static_cast<std::int32_t>(rhs16);
                state.macl = static_cast<std::uint32_t>(product);
            } else if (instruction.op == Sh4IrOp::multiply_unsigned_word) {
                const auto lhs16 = static_cast<std::uint32_t>(lhs & 0xFFFFu);
                const auto rhs16 = static_cast<std::uint32_t>(rhs & 0xFFFFu);
                state.macl = lhs16 * rhs16;
            } else {
                std::uint64_t product{};
                if (instruction.op == Sh4IrOp::multiply_signed_long) {
                    const auto signed_product = static_cast<std::int64_t>(as_signed(lhs)) *
                                                static_cast<std::int64_t>(as_signed(rhs));
                    product = static_cast<std::uint64_t>(signed_product);
                } else {
                    product = static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs);
                }
                state.mach = static_cast<std::uint32_t>(product >> 32u);
                state.macl = static_cast<std::uint32_t>(product);
            }
            return Result<void>::success();
        }

        case Sh4IrOp::sign_extend_byte:
        case Sh4IrOp::sign_extend_word:
        case Sh4IrOp::zero_extend_byte:
        case Sh4IrOp::zero_extend_word:
        case Sh4IrOp::swap_low_bytes:
        case Sh4IrOp::swap_words:
        case Sh4IrOp::extract_middle: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto original_dst = state.r[instruction.dst_reg];
            const auto original_src = state.r[instruction.src_reg];
            std::uint32_t result{};
            if (instruction.op == Sh4IrOp::sign_extend_byte) {
                const auto value = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(original_src & 0xFFu));
                result = static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
            } else if (instruction.op == Sh4IrOp::sign_extend_word) {
                const auto value = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(original_src & 0xFFFFu));
                result = static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
            } else if (instruction.op == Sh4IrOp::zero_extend_byte) {
                result = original_src & 0xFFu;
            } else if (instruction.op == Sh4IrOp::zero_extend_word) {
                result = original_src & 0xFFFFu;
            } else if (instruction.op == Sh4IrOp::swap_low_bytes) {
                result = (original_src & 0xFFFF0000u) |
                         ((original_src & 0x000000FFu) << 8u) |
                         ((original_src & 0x0000FF00u) >> 8u);
            } else if (instruction.op == Sh4IrOp::swap_words) {
                result = (original_src << 16u) | (original_src >> 16u);
            } else {
                result = (original_src << 16u) | (original_dst >> 16u);
            }
            state.r[instruction.dst_reg] = result;
            return Result<void>::success();
        }

        case Sh4IrOp::add_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] += state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::add_with_carry: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto carry_in = state.t ? 1u : 0u;
            const auto sum = static_cast<std::uint64_t>(state.r[instruction.dst_reg]) +
                             static_cast<std::uint64_t>(state.r[instruction.src_reg]) + carry_in;
            state.r[instruction.dst_reg] = static_cast<std::uint32_t>(sum);
            state.t = (sum >> 32u) != 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::add_with_overflow: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto left = state.r[instruction.dst_reg];
            const auto right = state.r[instruction.src_reg];
            const auto result = left + right;
            state.r[instruction.dst_reg] = result;
            state.t = ((~(left ^ right) & (left ^ result)) & 0x80000000u) != 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::sub_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] -= state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::sub_with_borrow: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto left = state.r[instruction.dst_reg];
            const auto right = state.r[instruction.src_reg];
            const auto borrow_in = state.t ? 1u : 0u;
            const auto subtrahend = static_cast<std::uint64_t>(right) + borrow_in;
            state.r[instruction.dst_reg] = left - right - borrow_in;
            state.t = static_cast<std::uint64_t>(left) < subtrahend;
            return Result<void>::success();
        }
        case Sh4IrOp::sub_with_overflow: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto left = state.r[instruction.dst_reg];
            const auto right = state.r[instruction.src_reg];
            const auto result = left - right;
            state.r[instruction.dst_reg] = result;
            state.t = (((left ^ right) & (left ^ result)) & 0x80000000u) != 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::negate_with_borrow: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto right = state.r[instruction.src_reg];
            const auto borrow_in = state.t ? 1u : 0u;
            const auto subtrahend = static_cast<std::uint64_t>(right) + borrow_in;
            state.r[instruction.dst_reg] = 0u - right - borrow_in;
            state.t = subtrahend != 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::compare_string_bytes: {
            auto lhs = require_register(instruction.dst_reg);
            if (!lhs) return lhs;
            auto rhs = require_register(instruction.src_reg);
            if (!rhs) return rhs;
            const auto value = state.r[instruction.dst_reg] ^ state.r[instruction.src_reg];
            state.t = (value & 0x000000FFu) == 0u ||
                      (value & 0x0000FF00u) == 0u ||
                      (value & 0x00FF0000u) == 0u ||
                      (value & 0xFF000000u) == 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::divide_init_unsigned:
            state.sr &= ~(kSrQBit | kSrMBit);
            state.t = false;
            return Result<void>::success();
        case Sh4IrOp::divide_init_signed: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const bool q = (state.r[instruction.dst_reg] & 0x80000000u) != 0u;
            const bool m = (state.r[instruction.src_reg] & 0x80000000u) != 0u;
            state.sr = (state.sr & ~(kSrQBit | kSrMBit)) |
                       (q ? kSrQBit : 0u) | (m ? kSrMBit : 0u);
            state.t = q != m;
            return Result<void>::success();
        }
        case Sh4IrOp::divide_step: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            bool old_q = (state.sr & kSrQBit) != 0u;
            const bool m = (state.sr & kSrMBit) != 0u;
            bool q = (state.r[instruction.dst_reg] & 0x80000000u) != 0u;
            auto& rn = state.r[instruction.dst_reg];
            const auto rm = state.r[instruction.src_reg];
            rn = (rn << 1u) | (state.t ? 1u : 0u);
            std::uint32_t before{};
            bool carry_or_borrow{};
            if (!old_q && !m) {
                before = rn; rn -= rm; carry_or_borrow = rn > before;
                q = q ? !carry_or_borrow : carry_or_borrow;
            } else if (!old_q && m) {
                before = rn; rn += rm; carry_or_borrow = rn < before;
                q = q ? carry_or_borrow : !carry_or_borrow;
            } else if (old_q && !m) {
                before = rn; rn += rm; carry_or_borrow = rn < before;
                q = q ? !carry_or_borrow : carry_or_borrow;
            } else {
                before = rn; rn -= rm; carry_or_borrow = rn > before;
                q = q ? carry_or_borrow : !carry_or_borrow;
            }
            if (q) state.sr |= kSrQBit; else state.sr &= ~kSrQBit;
            state.t = q == m;
            return Result<void>::success();
        }
        case Sh4IrOp::compare_eq: {
            auto lhs = require_register(instruction.dst_reg);
            if (!lhs) return lhs;
            auto rhs = require_register(instruction.src_reg);
            if (!rhs) return rhs;
            state.t = state.r[instruction.dst_reg] == state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::compare_eq_imm: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.t = state.r[instruction.dst_reg] == static_cast<std::uint32_t>(instruction.imm);
            return Result<void>::success();
        }
        case Sh4IrOp::compare_unsigned_ge:
        case Sh4IrOp::compare_signed_ge:
        case Sh4IrOp::compare_unsigned_gt:
        case Sh4IrOp::compare_signed_gt: {
            auto lhs = require_register(instruction.dst_reg);
            if (!lhs) return lhs;
            auto rhs = require_register(instruction.src_reg);
            if (!rhs) return rhs;
            const auto left = state.r[instruction.dst_reg];
            const auto right = state.r[instruction.src_reg];
            if (instruction.op == Sh4IrOp::compare_unsigned_ge) state.t = left >= right;
            if (instruction.op == Sh4IrOp::compare_signed_ge) state.t = as_signed(left) >= as_signed(right);
            if (instruction.op == Sh4IrOp::compare_unsigned_gt) state.t = left > right;
            if (instruction.op == Sh4IrOp::compare_signed_gt) state.t = as_signed(left) > as_signed(right);
            return Result<void>::success();
        }
        case Sh4IrOp::compare_pz:
        case Sh4IrOp::compare_pl: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            const auto value = as_signed(state.r[instruction.dst_reg]);
            state.t = instruction.op == Sh4IrOp::compare_pz ? value >= 0 : value > 0;
            return Result<void>::success();
        }
        case Sh4IrOp::decrement_and_test: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            --state.r[instruction.dst_reg];
            state.t = state.r[instruction.dst_reg] == 0u;
            return Result<void>::success();
        }
        case Sh4IrOp::test_bits_reg:
        case Sh4IrOp::bit_and_reg:
        case Sh4IrOp::bit_xor_reg:
        case Sh4IrOp::bit_or_reg: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if (instruction.op == Sh4IrOp::test_bits_reg) state.t = (state.r[instruction.dst_reg] & state.r[instruction.src_reg]) == 0u;
            else if (instruction.op == Sh4IrOp::bit_and_reg) state.r[instruction.dst_reg] &= state.r[instruction.src_reg];
            else if (instruction.op == Sh4IrOp::bit_xor_reg) state.r[instruction.dst_reg] ^= state.r[instruction.src_reg];
            else state.r[instruction.dst_reg] |= state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::test_bits_imm:
        case Sh4IrOp::bit_and_imm:
        case Sh4IrOp::bit_xor_imm:
        case Sh4IrOp::bit_or_imm: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            const auto value = static_cast<std::uint32_t>(instruction.imm);
            if (instruction.op == Sh4IrOp::test_bits_imm) state.t = (state.r[instruction.dst_reg] & value) == 0u;
            else if (instruction.op == Sh4IrOp::bit_and_imm) state.r[instruction.dst_reg] &= value;
            else if (instruction.op == Sh4IrOp::bit_xor_imm) state.r[instruction.dst_reg] ^= value;
            else state.r[instruction.dst_reg] |= value;
            return Result<void>::success();
        }
        case Sh4IrOp::test_gbr_byte_imm:
        case Sh4IrOp::and_gbr_byte_imm:
        case Sh4IrOp::xor_gbr_byte_imm:
        case Sh4IrOp::or_gbr_byte_imm: {
            const auto address = state.gbr + state.r[0];
            auto loaded = read_u8(memory, address);
            if (!loaded) return Result<void>::failure(loaded.error, loaded.detail);
            const auto immediate = static_cast<std::uint8_t>(instruction.imm & 0xFF);
            if (instruction.op == Sh4IrOp::test_gbr_byte_imm) {
                state.t = (loaded.value & immediate) == 0u;
                return Result<void>::success();
            }
            std::uint8_t result = loaded.value;
            if (instruction.op == Sh4IrOp::and_gbr_byte_imm) result &= immediate;
            else if (instruction.op == Sh4IrOp::xor_gbr_byte_imm) result ^= immediate;
            else result |= immediate;
            return write_u8(memory, address, result);
        }
        case Sh4IrOp::bit_not:
        case Sh4IrOp::negate: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.r[instruction.dst_reg] = instruction.op == Sh4IrOp::bit_not
                ? ~state.r[instruction.src_reg]
                : 0u - state.r[instruction.src_reg];
            return Result<void>::success();
        }
        case Sh4IrOp::shift_arithmetic_dynamic:
        case Sh4IrOp::shift_logical_dynamic: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto count_value = state.r[instruction.src_reg];
            const auto value = state.r[instruction.dst_reg];
            if ((count_value & 0x80000000u) == 0u) {
                state.r[instruction.dst_reg] = value << (count_value & 31u);
            } else {
                const auto count = static_cast<unsigned>(((~count_value) & 31u) + 1u);
                if (instruction.op == Sh4IrOp::shift_logical_dynamic) {
                    state.r[instruction.dst_reg] = count >= 32u ? 0u : value >> count;
                } else if (count >= 32u) {
                    state.r[instruction.dst_reg] = (value & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u;
                } else {
                    auto shifted = value >> count;
                    if ((value & 0x80000000u) != 0u) shifted |= (~0u << (32u - count));
                    state.r[instruction.dst_reg] = shifted;
                }
            }
            return Result<void>::success();
        }
        case Sh4IrOp::shift_left_one:
        case Sh4IrOp::shift_right_logical_one:
        case Sh4IrOp::shift_right_arithmetic_one:
        case Sh4IrOp::rotate_left_one:
        case Sh4IrOp::rotate_right_one:
        case Sh4IrOp::rotate_left_through_t:
        case Sh4IrOp::rotate_right_through_t: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            const auto value = state.r[instruction.dst_reg];
            const bool old_t = state.t;
            if (instruction.op == Sh4IrOp::shift_left_one) {
                state.t = (value & 0x80000000u) != 0u;
                state.r[instruction.dst_reg] = value << 1u;
            } else if (instruction.op == Sh4IrOp::shift_right_logical_one) {
                state.t = (value & 1u) != 0u;
                state.r[instruction.dst_reg] = value >> 1u;
            } else if (instruction.op == Sh4IrOp::shift_right_arithmetic_one) {
                state.t = (value & 1u) != 0u;
                state.r[instruction.dst_reg] = (value >> 1u) | (value & 0x80000000u);
            } else if (instruction.op == Sh4IrOp::rotate_left_one) {
                state.t = (value & 0x80000000u) != 0u;
                state.r[instruction.dst_reg] = (value << 1u) | (value >> 31u);
            } else if (instruction.op == Sh4IrOp::rotate_right_one) {
                state.t = (value & 1u) != 0u;
                state.r[instruction.dst_reg] = (value >> 1u) | (value << 31u);
            } else if (instruction.op == Sh4IrOp::rotate_left_through_t) {
                state.t = (value & 0x80000000u) != 0u;
                state.r[instruction.dst_reg] = (value << 1u) | (old_t ? 1u : 0u);
            } else {
                state.t = (value & 1u) != 0u;
                state.r[instruction.dst_reg] = (value >> 1u) | (old_t ? 0x80000000u : 0u);
            }
            return Result<void>::success();
        }
        case Sh4IrOp::shift_left_const:
        case Sh4IrOp::shift_right_logical_const: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            if (instruction.imm != 2 && instruction.imm != 8 && instruction.imm != 16) {
                return Result<void>::failure(ErrorCode::invalid_argument,
                                             "SH-4 constant shift count is unsupported");
            }
            const auto count = static_cast<unsigned>(instruction.imm);
            if (instruction.op == Sh4IrOp::shift_left_const) state.r[instruction.dst_reg] <<= count;
            else state.r[instruction.dst_reg] >>= count;
            return Result<void>::success();
        }
        case Sh4IrOp::branch_direct:
            pending = PendingTransfer{instruction.target, std::nullopt};
            return Result<void>::success();
        case Sh4IrOp::branch_if_t:
            pending = PendingTransfer{instruction.target, state.t};
            return Result<void>::success();
        case Sh4IrOp::branch_if_not_t:
            pending = PendingTransfer{instruction.target, !state.t};
            return Result<void>::success();
        case Sh4IrOp::call_direct:
            state.pr = instruction.source_address + 4u;
            pending = PendingTransfer{instruction.target, std::nullopt};
            return Result<void>::success();
        case Sh4IrOp::branch_reg_relative: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            pending = PendingTransfer{instruction.source_address + 4u + state.r[instruction.src_reg], std::nullopt};
            return Result<void>::success();
        }
        case Sh4IrOp::jump_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            pending = PendingTransfer{state.r[instruction.src_reg], std::nullopt};
            return Result<void>::success();
        }
        case Sh4IrOp::call_reg: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto target = state.r[instruction.src_reg];
            state.pr = instruction.source_address + 4u;
            pending = PendingTransfer{target, std::nullopt};
            return Result<void>::success();
        }
        case Sh4IrOp::call_reg_relative: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            const auto target = instruction.source_address + 4u + state.r[instruction.src_reg];
            state.pr = instruction.source_address + 4u;
            pending = PendingTransfer{target, std::nullopt};
            return Result<void>::success();
        }
        case Sh4IrOp::return_pr:
            pending = PendingTransfer{state.pr, std::nullopt};
            return Result<void>::success();
        case Sh4IrOp::return_exception:
            if (!require_privileged(instruction, state, pending)) return Result<void>::success();
            write_sh4_reference_sr(state, state.ssr);
            pending = PendingTransfer{state.spc, std::nullopt};
            return Result<void>::success();
        case Sh4IrOp::load_pc_word: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto value = read_u16(memory, instruction.target);
            if (!value) return Result<void>::failure(value.error, value.detail);
            const auto signed_value = std::bit_cast<std::int16_t>(value.value);
            state.r[instruction.dst_reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(signed_value));
            return Result<void>::success();
        }
        case Sh4IrOp::load_pc_long: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto value = read_u32(memory, instruction.target);
            if (!value) return Result<void>::failure(value.error, value.detail);
            state.r[instruction.dst_reg] = value.value;
            return Result<void>::success();
        }
        case Sh4IrOp::load_pc_address: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.r[instruction.dst_reg] = instruction.target;
            return Result<void>::success();
        }
    }
    return Result<void>::failure(ErrorCode::unsupported_format,
                                 "reference executor encountered an unknown SH-4 IR operation");
}

Result<std::uint32_t> resolve_exit(const Sh4IrBlock& block,
                                   const std::optional<PendingTransfer>& pending,
                                   Sh4ReferenceStopReason& stop_reason) {
    switch (block.exit) {
        case Sh4IrExit::end_of_stream:
            stop_reason = Sh4ReferenceStopReason::end_of_stream;
            return Result<std::uint32_t>::success(block.ops.empty()
                ? block.start_address
                : block.ops.back().source_address + 2u);
        case Sh4IrExit::fallthrough:
            if (!block.fallthrough_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "IR fallthrough block is missing its target");
            return Result<std::uint32_t>::success(*block.fallthrough_target);
        case Sh4IrExit::conditional_branch:
            if (!pending || !pending->condition || !block.fallthrough_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "IR conditional block is missing a latched branch decision or fallthrough");
            if (block.branch_target && pending->target != *block.branch_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "latched conditional target disagrees with CFG metadata");
            return Result<std::uint32_t>::success(*pending->condition ? pending->target : *block.fallthrough_target);
        case Sh4IrExit::direct_branch:
        case Sh4IrExit::direct_call:
        case Sh4IrExit::indirect_call:
        case Sh4IrExit::indirect_jump:
        case Sh4IrExit::return_subroutine:
        case Sh4IrExit::return_exception:
            if (!pending) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "IR control-flow block did not latch a target");
            if ((block.exit == Sh4IrExit::direct_branch || block.exit == Sh4IrExit::direct_call) && block.branch_target && pending->target != *block.branch_target) return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "latched direct target disagrees with CFG metadata");
            return Result<std::uint32_t>::success(pending->target);
    }
    return Result<std::uint32_t>::failure(ErrorCode::unsupported_format,
                                          "reference executor encountered an unknown IR block exit");
}

}

Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceMemoryView memory,
    std::size_t max_blocks) {
    if (max_blocks == 0) return Result<Sh4ReferenceRunResult>::failure(ErrorCode::invalid_argument, "reference executor block limit must be non-zero");
    if (!find_sh4_ir_block(program, program.entry_address)) return Result<Sh4ReferenceRunResult>::failure(ErrorCode::invalid_argument, "reference executor entry block is missing");

    Sh4ReferenceRunResult run{};
    state.pc = program.entry_address;
    while (run.blocks_executed < max_blocks) {
        const auto* block = find_sh4_ir_block(program, state.pc);
        if (!block) {
            run.stop_reason = Sh4ReferenceStopReason::left_program;
            return Result<Sh4ReferenceRunResult>::success(run);
        }
        std::optional<PendingTransfer> pending;
        for (const auto& instruction : block->ops) {
            auto executed = execute_op(instruction, state, memory, pending);
            if (!executed) return Result<Sh4ReferenceRunResult>::failure(executed.error, executed.detail);
            ++run.operations_executed;
            if (state.sleeping) {
                ++run.blocks_executed;
                state.pc = instruction.source_address + 2u;
                run.stop_reason = Sh4ReferenceStopReason::sleep;
                return Result<Sh4ReferenceRunResult>::success(run);
            }
            if (pending && pending->immediate) break;
        }
        ++run.blocks_executed;
        if (pending && pending->immediate) {
            state.pc = pending->target;
            if (!find_sh4_ir_block(program, state.pc)) {
                run.stop_reason = Sh4ReferenceStopReason::left_program;
                return Result<Sh4ReferenceRunResult>::success(run);
            }
            continue;
        }
        Sh4ReferenceStopReason exit_reason{Sh4ReferenceStopReason::left_program};
        auto next_pc = resolve_exit(*block, pending, exit_reason);
        if (!next_pc) return Result<Sh4ReferenceRunResult>::failure(next_pc.error, next_pc.detail);
        state.pc = next_pc.value;
        if (block->exit == Sh4IrExit::end_of_stream) {
            run.stop_reason = exit_reason;
            return Result<Sh4ReferenceRunResult>::success(run);
        }
        if (!find_sh4_ir_block(program, state.pc)) {
            run.stop_reason = Sh4ReferenceStopReason::left_program;
            return Result<Sh4ReferenceRunResult>::success(run);
        }
    }
    run.stop_reason = Sh4ReferenceStopReason::block_limit;
    return Result<Sh4ReferenceRunResult>::success(run);
}

}
