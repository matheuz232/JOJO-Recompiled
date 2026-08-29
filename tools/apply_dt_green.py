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
    "    cmp_pz,\n    cmp_pl,\n    tst_reg,",
    "    cmp_pz,\n    cmp_pl,\n    dt,\n    tst_reg,",
)

replace_once(
    "src/core/sh4_decoder.cpp",
    "    if ((raw & 0xF0FFu) == 0x4015u) {\n        i.op = Sh4Op::cmp_pl;\n        i.rn = n_field(raw);\n        return i;\n    }\n\n    if ((raw & 0xF00Fu) == 0x2008u ||",
    "    if ((raw & 0xF0FFu) == 0x4015u) {\n        i.op = Sh4Op::cmp_pl;\n        i.rn = n_field(raw);\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0x4010u) {\n        i.op = Sh4Op::dt;\n        i.rn = n_field(raw);\n        return i;\n    }\n\n    if ((raw & 0xF00Fu) == 0x2008u ||",
)

replace_once(
    "src/core/sh4_ir.h",
    "    compare_pz,\n    compare_pl,\n    test_bits_reg,",
    "    compare_pz,\n    compare_pl,\n    decrement_and_test,\n    test_bits_reg,",
)

replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::cmp_pz: out.op = Sh4IrOp::compare_pz; break;\n        case Sh4Op::cmp_pl: out.op = Sh4IrOp::compare_pl; break;\n        case Sh4Op::tst_reg:",
    "        case Sh4Op::cmp_pz: out.op = Sh4IrOp::compare_pz; break;\n        case Sh4Op::cmp_pl: out.op = Sh4IrOp::compare_pl; break;\n        case Sh4Op::dt: out.op = Sh4IrOp::decrement_and_test; break;\n        case Sh4Op::tst_reg:",
)

replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::compare_pz:\n        case Sh4IrOp::compare_pl: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            const auto value = as_signed(state.r[instruction.dst_reg]);\n            state.t = instruction.op == Sh4IrOp::compare_pz ? value >= 0 : value > 0;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::test_bits_reg:",
    "        case Sh4IrOp::compare_pz:\n        case Sh4IrOp::compare_pl: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            const auto value = as_signed(state.r[instruction.dst_reg]);\n            state.t = instruction.op == Sh4IrOp::compare_pz ? value >= 0 : value > 0;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::decrement_and_test: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            --state.r[instruction.dst_reg];\n            state.t = state.r[instruction.dst_reg] == 0u;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::test_bits_reg:",
)

Path(__file__).unlink()
Path(".github/workflows/apply-dt-green.yml").unlink()
