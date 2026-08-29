from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/sh4_decoder.h",
    "    shll,\n    shlr,\n    shar,\n    shll2,",
    "    shll,\n    shlr,\n    shar,\n    rotl,\n    rotr,\n    rotcl,\n    rotcr,\n    shll2,",
)

replace_once(
    "src/core/sh4_decoder.cpp",
    "    const auto shift_code = static_cast<std::uint16_t>(raw & 0xF0FFu);\n    if (shift_code == 0x4000u || shift_code == 0x4001u || shift_code == 0x4021u ||\n        shift_code == 0x4008u || shift_code == 0x4009u || shift_code == 0x4018u ||\n        shift_code == 0x4019u || shift_code == 0x4028u || shift_code == 0x4029u) {\n        if (shift_code == 0x4000u) i.op = Sh4Op::shll;\n        if (shift_code == 0x4001u) i.op = Sh4Op::shlr;\n        if (shift_code == 0x4021u) i.op = Sh4Op::shar;\n        if (shift_code == 0x4008u) i.op = Sh4Op::shll2;\n        if (shift_code == 0x4009u) i.op = Sh4Op::shlr2;\n        if (shift_code == 0x4018u) i.op = Sh4Op::shll8;\n        if (shift_code == 0x4019u) i.op = Sh4Op::shlr8;\n        if (shift_code == 0x4028u) i.op = Sh4Op::shll16;\n        if (shift_code == 0x4029u) i.op = Sh4Op::shlr16;\n        i.rn = n_field(raw);\n        return i;\n    }",
    "    const auto shift_code = static_cast<std::uint16_t>(raw & 0xF0FFu);\n    if (shift_code == 0x4000u || shift_code == 0x4001u || shift_code == 0x4021u ||\n        shift_code == 0x4004u || shift_code == 0x4005u || shift_code == 0x4024u ||\n        shift_code == 0x4025u || shift_code == 0x4008u || shift_code == 0x4009u ||\n        shift_code == 0x4018u || shift_code == 0x4019u || shift_code == 0x4028u ||\n        shift_code == 0x4029u) {\n        if (shift_code == 0x4000u) i.op = Sh4Op::shll;\n        if (shift_code == 0x4001u) i.op = Sh4Op::shlr;\n        if (shift_code == 0x4021u) i.op = Sh4Op::shar;\n        if (shift_code == 0x4004u) i.op = Sh4Op::rotl;\n        if (shift_code == 0x4005u) i.op = Sh4Op::rotr;\n        if (shift_code == 0x4024u) i.op = Sh4Op::rotcl;\n        if (shift_code == 0x4025u) i.op = Sh4Op::rotcr;\n        if (shift_code == 0x4008u) i.op = Sh4Op::shll2;\n        if (shift_code == 0x4009u) i.op = Sh4Op::shlr2;\n        if (shift_code == 0x4018u) i.op = Sh4Op::shll8;\n        if (shift_code == 0x4019u) i.op = Sh4Op::shlr8;\n        if (shift_code == 0x4028u) i.op = Sh4Op::shll16;\n        if (shift_code == 0x4029u) i.op = Sh4Op::shlr16;\n        i.rn = n_field(raw);\n        return i;\n    }",
)

replace_once(
    "src/core/sh4_ir.h",
    "    shift_left_one,\n    shift_right_logical_one,\n    shift_right_arithmetic_one,\n    shift_left_const,",
    "    shift_left_one,\n    shift_right_logical_one,\n    shift_right_arithmetic_one,\n    rotate_left_one,\n    rotate_right_one,\n    rotate_left_through_t,\n    rotate_right_through_t,\n    shift_left_const,",
)

replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::shll: out.op = Sh4IrOp::shift_left_one; break;\n        case Sh4Op::shlr: out.op = Sh4IrOp::shift_right_logical_one; break;\n        case Sh4Op::shar: out.op = Sh4IrOp::shift_right_arithmetic_one; break;\n        case Sh4Op::shll2: out.op = Sh4IrOp::shift_left_const; out.imm = 2; break;",
    "        case Sh4Op::shll: out.op = Sh4IrOp::shift_left_one; break;\n        case Sh4Op::shlr: out.op = Sh4IrOp::shift_right_logical_one; break;\n        case Sh4Op::shar: out.op = Sh4IrOp::shift_right_arithmetic_one; break;\n        case Sh4Op::rotl: out.op = Sh4IrOp::rotate_left_one; break;\n        case Sh4Op::rotr: out.op = Sh4IrOp::rotate_right_one; break;\n        case Sh4Op::rotcl: out.op = Sh4IrOp::rotate_left_through_t; break;\n        case Sh4Op::rotcr: out.op = Sh4IrOp::rotate_right_through_t; break;\n        case Sh4Op::shll2: out.op = Sh4IrOp::shift_left_const; out.imm = 2; break;",
)

replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::shift_left_one:\n        case Sh4IrOp::shift_right_logical_one:\n        case Sh4IrOp::shift_right_arithmetic_one: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            const auto value = state.r[instruction.dst_reg];\n            if (instruction.op == Sh4IrOp::shift_left_one) {\n                state.t = (value & 0x80000000u) != 0u;\n                state.r[instruction.dst_reg] = value << 1u;\n            } else if (instruction.op == Sh4IrOp::shift_right_logical_one) {\n                state.t = (value & 1u) != 0u;\n                state.r[instruction.dst_reg] = value >> 1u;\n            } else {\n                state.t = (value & 1u) != 0u;\n                state.r[instruction.dst_reg] = (value >> 1u) | (value & 0x80000000u);\n            }\n            return Result<void>::success();\n        }\n        case Sh4IrOp::shift_left_const:",
    "        case Sh4IrOp::shift_left_one:\n        case Sh4IrOp::shift_right_logical_one:\n        case Sh4IrOp::shift_right_arithmetic_one:\n        case Sh4IrOp::rotate_left_one:\n        case Sh4IrOp::rotate_right_one:\n        case Sh4IrOp::rotate_left_through_t:\n        case Sh4IrOp::rotate_right_through_t: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            const auto value = state.r[instruction.dst_reg];\n            const bool old_t = state.t;\n            if (instruction.op == Sh4IrOp::shift_left_one) {\n                state.t = (value & 0x80000000u) != 0u;\n                state.r[instruction.dst_reg] = value << 1u;\n            } else if (instruction.op == Sh4IrOp::shift_right_logical_one) {\n                state.t = (value & 1u) != 0u;\n                state.r[instruction.dst_reg] = value >> 1u;\n            } else if (instruction.op == Sh4IrOp::shift_right_arithmetic_one) {\n                state.t = (value & 1u) != 0u;\n                state.r[instruction.dst_reg] = (value >> 1u) | (value & 0x80000000u);\n            } else if (instruction.op == Sh4IrOp::rotate_left_one) {\n                state.t = (value & 0x80000000u) != 0u;\n                state.r[instruction.dst_reg] = (value << 1u) | (value >> 31u);\n            } else if (instruction.op == Sh4IrOp::rotate_right_one) {\n                state.t = (value & 1u) != 0u;\n                state.r[instruction.dst_reg] = (value >> 1u) | (value << 31u);\n            } else if (instruction.op == Sh4IrOp::rotate_left_through_t) {\n                state.t = (value & 0x80000000u) != 0u;\n                state.r[instruction.dst_reg] = (value << 1u) | (old_t ? 1u : 0u);\n            } else {\n                state.t = (value & 1u) != 0u;\n                state.r[instruction.dst_reg] = (value >> 1u) | (old_t ? 0x80000000u : 0u);\n            }\n            return Result<void>::success();\n        }\n        case Sh4IrOp::shift_left_const:",
)

Path(__file__).unlink()
Path(".github/workflows/apply-rotates-green.yml").unlink()
