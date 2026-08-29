from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Decoder enum.
replace_once(
    "src/core/sh4_decoder.h",
    "    fcmp_eq,\n    fcmp_gt,\n    fmov_reg,\n",
    "    fcmp_eq,\n    fcmp_gt,\n    fcnvsd,\n    fcnvds,\n    fipr,\n    fsca,\n    fsrra,\n    ftrv,\n    fmov_reg,\n",
)

# Decoder patterns. Keep fixed FRCHG/FSCHG behavior and insert final families before FMOV catch-all.
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (fpu_binary_code == 0xF004u || fpu_binary_code == 0xF005u) {\n        i.op = fpu_binary_code == 0xF004u ? Sh4Op::fcmp_eq : Sh4Op::fcmp_gt;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n    if ((raw & 0xF00Fu) == 0xF00Cu) {\n",
    "    if (fpu_binary_code == 0xF004u || fpu_binary_code == 0xF005u) {\n        i.op = fpu_binary_code == 0xF004u ? Sh4Op::fcmp_eq : Sh4Op::fcmp_gt;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0xF0ADu) {\n        i.op = Sh4Op::fcnvsd;\n        i.rn = n_field(raw);\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0xF0BDu) {\n        i.op = Sh4Op::fcnvds;\n        i.rm = n_field(raw);\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0xF0EDu) {\n        i.op = Sh4Op::fipr;\n        i.rn = static_cast<std::uint8_t>(((raw >> 10u) & 0x3u) * 4u);\n        i.rm = static_cast<std::uint8_t>(((raw >> 8u) & 0x3u) * 4u);\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0xF07Du) {\n        i.op = Sh4Op::fsrra;\n        i.rn = n_field(raw);\n        return i;\n    }\n    if ((raw & 0xF1FFu) == 0xF0FDu) {\n        i.op = Sh4Op::fsca;\n        i.rn = static_cast<std::uint8_t>(n_field(raw) & 0x0Eu);\n        return i;\n    }\n    if ((raw & 0xF3FFu) == 0xF1FDu) {\n        i.op = Sh4Op::ftrv;\n        i.rn = static_cast<std::uint8_t>(((raw >> 10u) & 0x3u) * 4u);\n        return i;\n    }\n    if ((raw & 0xF00Fu) == 0xF00Cu) {\n",
)

# IR enum + lift mappings.
replace_once(
    "src/core/sh4_ir.h",
    "    compare_single_float_eq,\n    compare_single_float_gt,\n    copy_fpu_registers,\n",
    "    compare_single_float_eq,\n    compare_single_float_gt,\n    convert_single_to_double,\n    convert_double_to_single,\n    inner_product_single,\n    sine_cosine_single,\n    reciprocal_sqrt_single,\n    matrix_transform_single,\n    copy_fpu_registers,\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::fcmp_eq: out.op = Sh4IrOp::compare_single_float_eq; break;\n        case Sh4Op::fcmp_gt: out.op = Sh4IrOp::compare_single_float_gt; break;\n        case Sh4Op::fmov_reg: out.op = Sh4IrOp::copy_fpu_registers; break;\n",
    "        case Sh4Op::fcmp_eq: out.op = Sh4IrOp::compare_single_float_eq; break;\n        case Sh4Op::fcmp_gt: out.op = Sh4IrOp::compare_single_float_gt; break;\n        case Sh4Op::fcnvsd: out.op = Sh4IrOp::convert_single_to_double; break;\n        case Sh4Op::fcnvds: out.op = Sh4IrOp::convert_double_to_single; break;\n        case Sh4Op::fipr: out.op = Sh4IrOp::inner_product_single; break;\n        case Sh4Op::fsca: out.op = Sh4IrOp::sine_cosine_single; break;\n        case Sh4Op::fsrra: out.op = Sh4IrOp::reciprocal_sqrt_single; break;\n        case Sh4Op::ftrv: out.op = Sh4IrOp::matrix_transform_single; break;\n        case Sh4Op::fmov_reg: out.op = Sh4IrOp::copy_fpu_registers; break;\n",
)

# FPSCR/SR masks.
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "constexpr std::uint32_t kFpscrRmMask = 0x00000003u;\nconstexpr std::uint32_t kSrQBit = 0x00000100u;\n",
    "constexpr std::uint32_t kFpscrRmMask = 0x00000003u;\nconstexpr std::uint32_t kFpscrCauseMask = 0x0003F000u;\nconstexpr std::uint32_t kFpscrCauseV = 0x00010000u;\nconstexpr std::uint32_t kFpscrCauseZ = 0x00008000u;\nconstexpr std::uint32_t kFpscrCauseO = 0x00004000u;\nconstexpr std::uint32_t kFpscrCauseU = 0x00002000u;\nconstexpr std::uint32_t kFpscrCauseI = 0x00001000u;\nconstexpr std::uint32_t kFpscrEnableMask = 0x00000F80u;\nconstexpr std::uint32_t kFpscrFlagMask = 0x0000007Cu;\nconstexpr std::uint32_t kSrFdBit = 0x00008000u;\nconstexpr std::uint32_t kSrQBit = 0x00000100u;\n",
)

# Pure FPU helpers are inserted before PendingTransfer.
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "struct PendingTransfer {\n",
    r'''struct Fpu32Eval {
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
                             std::uint32_t rhs_bits) noexcept {
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
                             std::uint64_t rhs_bits) noexcept {
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
    return op >= Sh4IrOp::set_fr_zero && op <= Sh4IrOp::toggle_fpscr_sz;
}

struct PendingTransfer {
''',
)

# Exception/flag helper after the general exception entry routine.
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "bool require_privileged(const Sh4IrInstruction& instruction,\n",
    r'''bool apply_fpu_cause(const Sh4IrInstruction& instruction,
                     Sh4ReferenceState& state,
                     std::optional<PendingTransfer>& pending,
                     std::uint32_t cause) {
    cause &= (kFpscrCauseV | kFpscrCauseZ | kFpscrCauseO | kFpscrCauseU | kFpscrCauseI);
    state.fpscr = (state.fpscr & ~kFpscrCauseMask) | cause;
    state.fpscr |= (cause >> 10u) & kFpscrFlagMask;
    const auto enabled = (cause >> 5u) & kFpscrEnableMask;
    if ((state.fpscr & enabled) != 0u) {
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
''',
)

# FPU-disable check at executor entry.
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "Result<void> execute_op(const Sh4IrInstruction& instruction,\n                        Sh4ReferenceState& state,\n                        Sh4ReferenceMemoryView memory,\n                        std::optional<PendingTransfer>& pending) {\n    switch (instruction.op) {\n",
    "Result<void> execute_op(const Sh4IrInstruction& instruction,\n                        Sh4ReferenceState& state,\n                        Sh4ReferenceMemoryView memory,\n                        std::optional<PendingTransfer>& pending) {\n    if (is_fpu_ir_op(instruction.op) && (state.sr & kSrFdBit) != 0u) {\n        enter_general_exception(instruction, state, pending,\n                                instruction.in_delay_slot ? 0x820u : 0x800u);\n        return Result<void>::success();\n    }\n    switch (instruction.op) {\n",
)

# Replace the FPU execution section wholesale while preserving memory/mode-switch behavior.
p = Path("src/core/sh4_reference_executor.cpp")
text = p.read_text(encoding="utf-8")
start_marker = "        case Sh4IrOp::set_fr_zero:"
end_marker = "        case Sh4IrOp::add_imm: {"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("executor FPU section markers not found")
new_fpu = r'''        case Sh4IrOp::set_fr_zero:
        case Sh4IrOp::set_fr_one: {
            auto reg = require_register(instruction.dst_reg);
            if (!reg) return reg;
            state.fr[instruction.dst_reg] = instruction.op == Sh4IrOp::set_fr_zero ? 0u : 0x3F800000u;
            state.fpscr &= ~kFpscrCauseMask;
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fr_to_fpul: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            state.fpul = state.fr[instruction.src_reg];
            state.fpscr &= ~kFpscrCauseMask;
            return Result<void>::success();
        }
        case Sh4IrOp::copy_fpul_to_fr: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            state.fr[instruction.dst_reg] = state.fpul;
            state.fpscr &= ~kFpscrCauseMask;
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
            state.fpscr &= ~kFpscrCauseMask;
            return Result<void>::success();
        }
        case Sh4IrOp::sqrt_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            std::uint32_t cause{};
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg)) return Result<void>::success();
                const auto value = normalize_double(state, read_dr_bits(state, instruction.dst_reg));
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
                const auto value = normalize_single(state, state.fr[instruction.dst_reg]);
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
                const auto lhs = normalize_double(state, read_dr_bits(state, instruction.dst_reg));
                const auto rhs = normalize_double(state, read_dr_bits(state, instruction.src_reg));
                if (std::isnan(lhs) || std::isnan(rhs)) cause = kFpscrCauseV;
                else value = instruction.op == Sh4IrOp::compare_single_float_eq ? lhs == rhs : lhs > rhs;
            } else {
                const auto lhs = normalize_single(state, state.fr[instruction.dst_reg]);
                const auto rhs = normalize_single(state, state.fr[instruction.src_reg]);
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
            if ((state.fpscr & kFpscrPrBit) == 0u || !require_fpu_pair(instruction, state, pending, instruction.dst_reg)) {
                if ((state.fpscr & kFpscrPrBit) == 0u) enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
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
            if ((state.fpscr & kFpscrPrBit) == 0u || !require_fpu_pair(instruction, state, pending, instruction.src_reg)) {
                if ((state.fpscr & kFpscrPrBit) == 0u) enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);
                return Result<void>::success();
            }
            const auto value = normalize_double(state, read_dr_bits(state, instruction.src_reg));
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
                                                    state.fr[instruction.src_reg + index]);
                cause |= mul.cause;
                const auto add = eval_single_binary(Sh4IrOp::add_single_float, state,
                                                    std::bit_cast<std::uint32_t>(accumulator), mul.bits);
                cause |= add.cause;
                accumulator = std::bit_cast<float>(add.bits);
            }
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
                                                        state.xf[col * 4u + row], source[col]);
                    cause |= mul.cause;
                    const auto add = eval_single_binary(Sh4IrOp::add_single_float, state, acc, mul.bits);
                    cause |= add.cause;
                    acc = add.bits;
                }
                output[row] = acc;
            }
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
            state.fpscr &= ~kFpscrCauseMask;
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
            state.fpscr &= ~kFpscrCauseMask;
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
            state.fpscr &= ~kFpscrCauseMask;
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            return store_fpu_memory(state, memory, state.r[instruction.dst_reg], instruction.src_reg);
        }
        case Sh4IrOp::load_fpu_memory: {
            state.fpscr &= ~kFpscrCauseMask;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            return load_fpu_memory(state, memory, state.r[instruction.src_reg], instruction.dst_reg);
        }
        case Sh4IrOp::load_fpu_postincrement: {
            state.fpscr &= ~kFpscrCauseMask;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.src_reg];
            auto loaded = load_fpu_memory(state, memory, address, instruction.dst_reg);
            if (!loaded) return loaded;
            state.r[instruction.src_reg] += fpu_memory_width(state);
            return Result<void>::success();
        }
        case Sh4IrOp::store_fpu_predecrement: {
            state.fpscr &= ~kFpscrCauseMask;
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[instruction.dst_reg] - fpu_memory_width(state);
            auto stored = store_fpu_memory(state, memory, address, instruction.src_reg);
            if (!stored) return stored;
            state.r[instruction.dst_reg] = address;
            return Result<void>::success();
        }
        case Sh4IrOp::load_fpu_indexed: {
            state.fpscr &= ~kFpscrCauseMask;
            auto address_reg = require_register(instruction.src_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[0] + state.r[instruction.src_reg];
            return load_fpu_memory(state, memory, address, instruction.dst_reg);
        }
        case Sh4IrOp::store_fpu_indexed: {
            state.fpscr &= ~kFpscrCauseMask;
            auto address_reg = require_register(instruction.dst_reg);
            if (!address_reg) return address_reg;
            const auto address = state.r[0] + state.r[instruction.dst_reg];
            return store_fpu_memory(state, memory, address, instruction.src_reg);
        }
        case Sh4IrOp::toggle_fpscr_fr:
            state.fpscr &= ~kFpscrCauseMask;
            write_fpscr(state, state.fpscr ^ kFpscrFrBit);
            return Result<void>::success();
        case Sh4IrOp::toggle_fpscr_sz:
            state.fpscr &= ~kFpscrCauseMask;
            write_fpscr(state, state.fpscr ^ kFpscrSzBit);
            return Result<void>::success();
'''
text = text[:start] + new_fpu + text[end:]
p.write_text(text, encoding="utf-8")

# Build the pre-existing FPU-disable RED test.
p = Path("CMakeLists.txt")
cmake = p.read_text(encoding="utf-8")
if "jojo_sh4_fpu_disable_tests" not in cmake:
    cmake += "\nadd_executable(jojo_sh4_fpu_disable_tests tests/test_sh4_fpu_disable.cpp)\n"
    cmake += "target_link_libraries(jojo_sh4_fpu_disable_tests PRIVATE jojo_core)\n"
    cmake += "add_test(NAME jojo_sh4_fpu_disable_tests COMMAND jojo_sh4_fpu_disable_tests)\n"
p.write_text(cmake, encoding="utf-8")
