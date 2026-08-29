from pathlib import Path

path = Path("src/core/sh4_reference_executor.cpp")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, found {count}: {old[:100]!r}")
    text = text.replace(old, new, 1)


replace_once(
    "constexpr std::uint32_t kFpscrCauseMask = 0x0003F000u;\nconstexpr std::uint32_t kFpscrCauseV = 0x00010000u;",
    "constexpr std::uint32_t kFpscrCauseMask = 0x0003F000u;\nconstexpr std::uint32_t kFpscrCauseE = 0x00020000u;\nconstexpr std::uint32_t kFpscrCauseV = 0x00010000u;",
)

replace_once(
    "Fpu32Eval eval_single_binary(Sh4IrOp op,\n                             const Sh4ReferenceState& state,\n                             std::uint32_t lhs_bits,\n                             std::uint32_t rhs_bits) noexcept {\n    const auto lhs = normalize_single(state, lhs_bits);",
    "Fpu32Eval eval_single_binary(Sh4IrOp op,\n                             const Sh4ReferenceState& state,\n                             std::uint32_t lhs_bits,\n                             std::uint32_t rhs_bits,\n                             bool allow_denormal_inputs = false) noexcept {\n    if (!allow_denormal_inputs && (state.fpscr & kFpscrDnBit) == 0u &&\n        (is_single_subnormal(lhs_bits) || is_single_subnormal(rhs_bits))) {\n        return {lhs_bits, kFpscrCauseE};\n    }\n    const auto lhs = normalize_single(state, lhs_bits);",
)

replace_once(
    "Fpu64Eval eval_double_binary(Sh4IrOp op,\n                             const Sh4ReferenceState& state,\n                             std::uint64_t lhs_bits,\n                             std::uint64_t rhs_bits) noexcept {\n    const auto lhs = normalize_double(state, lhs_bits);",
    "Fpu64Eval eval_double_binary(Sh4IrOp op,\n                             const Sh4ReferenceState& state,\n                             std::uint64_t lhs_bits,\n                             std::uint64_t rhs_bits,\n                             bool allow_denormal_inputs = false) noexcept {\n    if (!allow_denormal_inputs && (state.fpscr & kFpscrDnBit) == 0u &&\n        (is_double_subnormal(lhs_bits) || is_double_subnormal(rhs_bits))) {\n        return {lhs_bits, kFpscrCauseE};\n    }\n    const auto lhs = normalize_double(state, lhs_bits);",
)

replace_once(
    "bool is_fpu_ir_op(Sh4IrOp op) noexcept {\n    return op >= Sh4IrOp::set_fr_zero && op <= Sh4IrOp::toggle_fpscr_sz;\n}",
    "bool is_fpu_ir_op(Sh4IrOp op) noexcept {\n    return (op >= Sh4IrOp::set_fr_zero && op <= Sh4IrOp::toggle_fpscr_sz) ||\n           op == Sh4IrOp::set_fpul_from_reg ||\n           op == Sh4IrOp::copy_fpul_to_reg ||\n           op == Sh4IrOp::load_fpul_postinc32 ||\n           op == Sh4IrOp::store_fpul_predec32 ||\n           op == Sh4IrOp::set_fpscr_from_reg ||\n           op == Sh4IrOp::copy_fpscr_to_reg ||\n           op == Sh4IrOp::load_fpscr_postinc32 ||\n           op == Sh4IrOp::store_fpscr_predec32;\n}",
)

replace_once(
    "    cause &= (kFpscrCauseV | kFpscrCauseZ | kFpscrCauseO | kFpscrCauseU | kFpscrCauseI);\n    state.fpscr = (state.fpscr & ~kFpscrCauseMask) | cause;\n    state.fpscr |= (cause >> 10u) & kFpscrFlagMask;\n    const auto enabled = (cause >> 5u) & kFpscrEnableMask;\n    if ((state.fpscr & enabled) != 0u) {",
    "    cause &= (kFpscrCauseE | kFpscrCauseV | kFpscrCauseZ | kFpscrCauseO | kFpscrCauseU | kFpscrCauseI);\n    state.fpscr = (state.fpscr & ~kFpscrCauseMask) | cause;\n    const auto sticky = cause & (kFpscrCauseV | kFpscrCauseZ | kFpscrCauseO | kFpscrCauseU | kFpscrCauseI);\n    state.fpscr |= (sticky >> 10u) & kFpscrFlagMask;\n    const auto enabled = (sticky >> 5u) & kFpscrEnableMask;\n    if ((cause & kFpscrCauseE) != 0u || (state.fpscr & enabled) != 0u) {",
)

# Operations documented as not modifying FPSCR must preserve the previous Cause field.
for line in [
    "            state.fpscr &= ~kFpscrCauseMask;\n",
]:
    # Remove every unconditional Cause clear in the no-exception FPU move/control section.
    pass

replace_once(
    "            state.fr[instruction.dst_reg] = instruction.op == Sh4IrOp::set_fr_zero ? 0u : 0x3F800000u;\n            state.fpscr &= ~kFpscrCauseMask;\n            return Result<void>::success();",
    "            state.fr[instruction.dst_reg] = instruction.op == Sh4IrOp::set_fr_zero ? 0u : 0x3F800000u;\n            return Result<void>::success();",
)
replace_once(
    "            state.fpul = state.fr[instruction.src_reg];\n            state.fpscr &= ~kFpscrCauseMask;\n            return Result<void>::success();",
    "            state.fpul = state.fr[instruction.src_reg];\n            return Result<void>::success();",
)
replace_once(
    "            state.fr[instruction.dst_reg] = state.fpul;\n            state.fpscr &= ~kFpscrCauseMask;\n            return Result<void>::success();",
    "            state.fr[instruction.dst_reg] = state.fpul;\n            return Result<void>::success();",
)
replace_once(
    "            if (instruction.op == Sh4IrOp::negate_single_float) state.fr[instruction.dst_reg] ^= 0x80000000u;\n            else state.fr[instruction.dst_reg] &= 0x7FFFFFFFu;\n            state.fpscr &= ~kFpscrCauseMask;\n            return Result<void>::success();",
    "            if (instruction.op == Sh4IrOp::negate_single_float) state.fr[instruction.dst_reg] ^= 0x80000000u;\n            else state.fr[instruction.dst_reg] &= 0x7FFFFFFFu;\n            return Result<void>::success();",
)

replace_once(
    "                const auto value = normalize_double(state, read_dr_bits(state, instruction.dst_reg));\n                double result{};",
    "                const auto operand_bits = read_dr_bits(state, instruction.dst_reg);\n                if ((state.fpscr & kFpscrDnBit) == 0u && is_double_subnormal(operand_bits)) {\n                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                    return Result<void>::success();\n                }\n                const auto value = normalize_double(state, operand_bits);\n                double result{};",
)
replace_once(
    "                const auto value = normalize_single(state, state.fr[instruction.dst_reg]);\n                float result{};",
    "                const auto operand_bits = state.fr[instruction.dst_reg];\n                if ((state.fpscr & kFpscrDnBit) == 0u && is_single_subnormal(operand_bits)) {\n                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                    return Result<void>::success();\n                }\n                const auto value = normalize_single(state, operand_bits);\n                float result{};",
)

replace_once(
    "            const auto a = normalize_single(state, state.fr[0]);\n            const auto b = normalize_single(state, state.fr[instruction.src_reg]);\n            const auto c = normalize_single(state, state.fr[instruction.dst_reg]);",
    "            if ((state.fpscr & kFpscrDnBit) == 0u &&\n                (is_single_subnormal(state.fr[0]) ||\n                 is_single_subnormal(state.fr[instruction.src_reg]) ||\n                 is_single_subnormal(state.fr[instruction.dst_reg]))) {\n                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                return Result<void>::success();\n            }\n            const auto a = normalize_single(state, state.fr[0]);\n            const auto b = normalize_single(state, state.fr[instruction.src_reg]);\n            const auto c = normalize_single(state, state.fr[instruction.dst_reg]);",
)

replace_once(
    "                const auto lhs = normalize_double(state, read_dr_bits(state, instruction.dst_reg));\n                const auto rhs = normalize_double(state, read_dr_bits(state, instruction.src_reg));",
    "                const auto lhs_bits = read_dr_bits(state, instruction.dst_reg);\n                const auto rhs_bits = read_dr_bits(state, instruction.src_reg);\n                if ((state.fpscr & kFpscrDnBit) == 0u &&\n                    (is_double_subnormal(lhs_bits) || is_double_subnormal(rhs_bits))) {\n                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                    return Result<void>::success();\n                }\n                const auto lhs = normalize_double(state, lhs_bits);\n                const auto rhs = normalize_double(state, rhs_bits);",
)
replace_once(
    "                const auto lhs = normalize_single(state, state.fr[instruction.dst_reg]);\n                const auto rhs = normalize_single(state, state.fr[instruction.src_reg]);",
    "                const auto lhs_bits = state.fr[instruction.dst_reg];\n                const auto rhs_bits = state.fr[instruction.src_reg];\n                if ((state.fpscr & kFpscrDnBit) == 0u &&\n                    (is_single_subnormal(lhs_bits) || is_single_subnormal(rhs_bits))) {\n                    apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                    return Result<void>::success();\n                }\n                const auto lhs = normalize_single(state, lhs_bits);\n                const auto rhs = normalize_single(state, rhs_bits);",
)

replace_once(
    "            if ((state.fpscr & kFpscrPrBit) == 0u || !require_fpu_pair(instruction, state, pending, instruction.dst_reg)) {\n                if ((state.fpscr & kFpscrPrBit) == 0u) enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);\n                return Result<void>::success();\n            }\n            const auto value = normalize_single(state, state.fpul);",
    "            if ((state.fpscr & kFpscrPrBit) == 0u || (state.fpscr & kFpscrSzBit) != 0u) {\n                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);\n                return Result<void>::success();\n            }\n            if (!require_fpu_pair(instruction, state, pending, instruction.dst_reg)) return Result<void>::success();\n            if ((state.fpscr & kFpscrDnBit) == 0u && is_single_subnormal(state.fpul)) {\n                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                return Result<void>::success();\n            }\n            const auto value = normalize_single(state, state.fpul);",
)
replace_once(
    "            if ((state.fpscr & kFpscrPrBit) == 0u || !require_fpu_pair(instruction, state, pending, instruction.src_reg)) {\n                if ((state.fpscr & kFpscrPrBit) == 0u) enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);\n                return Result<void>::success();\n            }\n            const auto value = normalize_double(state, read_dr_bits(state, instruction.src_reg));",
    "            if ((state.fpscr & kFpscrPrBit) == 0u || (state.fpscr & kFpscrSzBit) != 0u) {\n                enter_general_exception(instruction, state, pending, instruction.in_delay_slot ? 0x1A0u : 0x180u);\n                return Result<void>::success();\n            }\n            if (!require_fpu_pair(instruction, state, pending, instruction.src_reg)) return Result<void>::success();\n            const auto operand_bits = read_dr_bits(state, instruction.src_reg);\n            if ((state.fpscr & kFpscrDnBit) == 0u && is_double_subnormal(operand_bits)) {\n                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                return Result<void>::success();\n            }\n            const auto value = normalize_double(state, operand_bits);",
)

# Graphics/vector operations support denormal inputs directly and architecturally signal Inexact.
replace_once(
    "                const auto mul = eval_single_binary(Sh4IrOp::multiply_single_float, state,\n                                                    state.fr[instruction.dst_reg + index],\n                                                    state.fr[instruction.src_reg + index]);",
    "                const auto mul = eval_single_binary(Sh4IrOp::multiply_single_float, state,\n                                                    state.fr[instruction.dst_reg + index],\n                                                    state.fr[instruction.src_reg + index], true);",
)
replace_once(
    "                const auto add = eval_single_binary(Sh4IrOp::add_single_float, state,\n                                                    std::bit_cast<std::uint32_t>(accumulator), mul.bits);",
    "                const auto add = eval_single_binary(Sh4IrOp::add_single_float, state,\n                                                    std::bit_cast<std::uint32_t>(accumulator), mul.bits, true);",
)
replace_once(
    "            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();\n            state.fr[instruction.dst_reg + 3u] = std::bit_cast<std::uint32_t>(accumulator);",
    "            cause |= kFpscrCauseI;\n            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();\n            state.fr[instruction.dst_reg + 3u] = std::bit_cast<std::uint32_t>(accumulator);",
)
replace_once(
    "                    const auto mul = eval_single_binary(Sh4IrOp::multiply_single_float, state,\n                                                        state.xf[col * 4u + row], source[col]);",
    "                    const auto mul = eval_single_binary(Sh4IrOp::multiply_single_float, state,\n                                                        state.xf[col * 4u + row], source[col], true);",
)
replace_once(
    "                    const auto add = eval_single_binary(Sh4IrOp::add_single_float, state, acc, mul.bits);",
    "                    const auto add = eval_single_binary(Sh4IrOp::add_single_float, state, acc, mul.bits, true);",
)
replace_once(
    "            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();\n            for (std::uint8_t i = 0; i < 4u; ++i) state.fr[instruction.dst_reg + i] = output[i];",
    "            cause |= kFpscrCauseI;\n            if (!apply_fpu_cause(instruction, state, pending, cause)) return Result<void>::success();\n            for (std::uint8_t i = 0; i < 4u; ++i) state.fr[instruction.dst_reg + i] = output[i];",
)

# FSCA, FMOV and FRCHG/FSCHG do not clear the FPU Cause field.
replace_once(
    "            state.fpscr &= ~kFpscrCauseMask;\n            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(sine);",
    "            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(sine);",
)
replace_once(
    "            state.fpscr &= ~kFpscrCauseMask;\n            if ((state.fpscr & kFpscrSzBit) == 0u) {",
    "            if ((state.fpscr & kFpscrSzBit) == 0u) {",
)
for old, new in [
    ("        case Sh4IrOp::store_fpu_memory: {\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::store_fpu_memory: {"),
    ("        case Sh4IrOp::load_fpu_memory: {\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::load_fpu_memory: {"),
    ("        case Sh4IrOp::load_fpu_postincrement: {\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::load_fpu_postincrement: {"),
    ("        case Sh4IrOp::store_fpu_predecrement: {\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::store_fpu_predecrement: {"),
    ("        case Sh4IrOp::load_fpu_indexed: {\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::load_fpu_indexed: {"),
    ("        case Sh4IrOp::store_fpu_indexed: {\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::store_fpu_indexed: {"),
    ("        case Sh4IrOp::toggle_fpscr_fr:\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::toggle_fpscr_fr:"),
    ("        case Sh4IrOp::toggle_fpscr_sz:\n            state.fpscr &= ~kFpscrCauseMask;", "        case Sh4IrOp::toggle_fpscr_sz:"),
]:
    replace_once(old, new)

path.write_text(text)
Path(__file__).unlink()
