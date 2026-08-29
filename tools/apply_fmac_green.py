from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/sh4_decoder.h",
    "    fsqrt,\n    fadd,",
    "    fsqrt,\n    fmac,\n    fadd,",
)

replace_once(
    "src/core/sh4_decoder.cpp",
    "    const auto fpu_binary_code = static_cast<std::uint16_t>(raw & 0xF00Fu);\n",
    "    if ((raw & 0xF00Fu) == 0xF00Eu) {\n"
    "        i.op = Sh4Op::fmac;\n"
    "        i.rn = n_field(raw);\n"
    "        i.rm = m_field(raw);\n"
    "        return i;\n"
    "    }\n"
    "    const auto fpu_binary_code = static_cast<std::uint16_t>(raw & 0xF00Fu);\n",
)

replace_once(
    "src/core/sh4_ir.h",
    "    sqrt_single_float,\n    add_single_float,",
    "    sqrt_single_float,\n    multiply_add_single_float,\n    add_single_float,",
)

replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::fsqrt: out.op = Sh4IrOp::sqrt_single_float; break;\n"
    "        case Sh4Op::fadd: out.op = Sh4IrOp::add_single_float; break;",
    "        case Sh4Op::fsqrt: out.op = Sh4IrOp::sqrt_single_float; break;\n"
    "        case Sh4Op::fmac: out.op = Sh4IrOp::multiply_add_single_float; break;\n"
    "        case Sh4Op::fadd: out.op = Sh4IrOp::add_single_float; break;",
)

binary_tail = '''    return Result<std::uint32_t>::success(bits);
}

bool is_load8(Sh4IrOp op) noexcept {
'''
fmac_helper = '''    return Result<std::uint32_t>::success(bits);
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
'''
replace_once("src/core/sh4_reference_executor.cpp", binary_tail, fmac_helper)

binary_case = '''        case Sh4IrOp::add_single_float:
        case Sh4IrOp::subtract_single_float:
        case Sh4IrOp::multiply_single_float:
        case Sh4IrOp::divide_single_float: {
'''
fmac_case = '''        case Sh4IrOp::multiply_add_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                return Result<void>::failure(
                    ErrorCode::unsupported_format,
                    "reference FMAC double-precision mode is not implemented");
            }

            auto destination = read_single_operand(state, state.fr[instruction.dst_reg]);
            if (!destination) {
                return Result<void>::failure(destination.error, destination.detail);
            }
            auto multiplier = read_single_operand(state, state.fr[0]);
            if (!multiplier) {
                return Result<void>::failure(multiplier.error, multiplier.detail);
            }
            auto source = read_single_operand(state, state.fr[instruction.src_reg]);
            if (!source) return Result<void>::failure(source.error, source.detail);
            auto result = calculate_single_fmac(
                state, destination.value, multiplier.value, source.value);
            if (!result) return Result<void>::failure(result.error, result.detail);
            state.fr[instruction.dst_reg] = result.value;
            return Result<void>::success();
        }
        case Sh4IrOp::add_single_float:
        case Sh4IrOp::subtract_single_float:
        case Sh4IrOp::multiply_single_float:
        case Sh4IrOp::divide_single_float: {
'''
replace_once("src/core/sh4_reference_executor.cpp", binary_case, fmac_case)

Path(__file__).unlink()
Path(".github/workflows/apply-fmac-green.yml").unlink()
