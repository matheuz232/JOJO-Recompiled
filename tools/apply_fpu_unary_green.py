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
    "    float_fpul,\n    ftrc,\n    fadd,",
    "    float_fpul,\n    ftrc,\n    fneg,\n    fabs,\n    fsqrt,\n    fadd,",
)

replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (fpu_transfer_code == 0xF03Du) {\n        i.op = Sh4Op::ftrc;\n        i.rm = n_field(raw);\n        return i;\n    }\n    const auto fpu_binary_code",
    "    if (fpu_transfer_code == 0xF03Du) {\n        i.op = Sh4Op::ftrc;\n        i.rm = n_field(raw);\n        return i;\n    }\n    if (fpu_transfer_code == 0xF04Du || fpu_transfer_code == 0xF05Du ||\n        fpu_transfer_code == 0xF06Du) {\n        if (fpu_transfer_code == 0xF04Du) i.op = Sh4Op::fneg;\n        if (fpu_transfer_code == 0xF05Du) i.op = Sh4Op::fabs;\n        if (fpu_transfer_code == 0xF06Du) i.op = Sh4Op::fsqrt;\n        i.rn = n_field(raw);\n        return i;\n    }\n    const auto fpu_binary_code",
)

replace_once(
    "src/core/sh4_ir.h",
    "    convert_fpul_to_float,\n    truncate_float_to_fpul,\n    add_single_float,",
    "    convert_fpul_to_float,\n    truncate_float_to_fpul,\n    negate_single_float,\n    absolute_single_float,\n    sqrt_single_float,\n    add_single_float,",
)

replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::float_fpul: out.op = Sh4IrOp::convert_fpul_to_float; break;\n        case Sh4Op::ftrc: out.op = Sh4IrOp::truncate_float_to_fpul; break;\n        case Sh4Op::fadd:",
    "        case Sh4Op::float_fpul: out.op = Sh4IrOp::convert_fpul_to_float; break;\n        case Sh4Op::ftrc: out.op = Sh4IrOp::truncate_float_to_fpul; break;\n        case Sh4Op::fneg: out.op = Sh4IrOp::negate_single_float; break;\n        case Sh4Op::fabs: out.op = Sh4IrOp::absolute_single_float; break;\n        case Sh4Op::fsqrt: out.op = Sh4IrOp::sqrt_single_float; break;\n        case Sh4Op::fadd:",
)

executor_old = """            state.fpul =
                std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(truncated));
            return Result<void>::success();
        }
        case Sh4IrOp::add_single_float:
"""
executor_new = """            state.fpul =
                std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(truncated));
            return Result<void>::success();
        }
        case Sh4IrOp::negate_single_float:
        case Sh4IrOp::absolute_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                return Result<void>::failure(
                    ErrorCode::unsupported_format,
                    "reference FPU double-precision unary operations are not implemented");
            }
            if (instruction.op == Sh4IrOp::negate_single_float) {
                state.fr[instruction.dst_reg] ^= 0x80000000u;
            } else {
                state.fr[instruction.dst_reg] &= 0x7FFFFFFFu;
            }
            return Result<void>::success();
        }
        case Sh4IrOp::sqrt_single_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                return Result<void>::failure(
                    ErrorCode::unsupported_format,
                    "reference FPU double-precision square root is not implemented");
            }
            const auto rounding_mode = state.fpscr & kFpscrRmMask;
            if (rounding_mode > 1u) {
                return Result<void>::failure(
                    ErrorCode::unsupported_format,
                    "reference FSQRT encountered a reserved rounding mode");
            }
            auto operand = read_single_operand(state, state.fr[instruction.dst_reg]);
            if (!operand) return Result<void>::failure(operand.error, operand.detail);
            if (operand.value < 0.0f) {
                return Result<void>::failure(
                    ErrorCode::unsupported_format,
                    "reference FSQRT invalid-operation flags are not implemented");
            }
            const auto exact = std::sqrt(static_cast<double>(operand.value));
            auto result = static_cast<float>(exact);
            if (rounding_mode == 1u && static_cast<double>(result) > exact) {
                result = std::nextafter(result, 0.0f);
            }
            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(result);
            return Result<void>::success();
        }
        case Sh4IrOp::add_single_float:
"""
replace_once("src/core/sh4_reference_executor.cpp", executor_old, executor_new)

Path("tools/fpu-unary-green.patch").unlink()
Path(__file__).unlink()
