from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"anchor missing in {path}: {old!r}")
    p.write_text(text.replace(old, new, 1))


replace(
    "src/core/sh4_decoder.h",
    "    flds,\n    fsts,\n    frchg,",
    "    flds,\n    fsts,\n    float_fpul,\n    ftrc,\n    frchg,",
)
replace(
    "src/core/sh4_decoder.cpp",
    "    if (raw == 0xFBFDu) {\n        i.op = Sh4Op::frchg;\n        return i;\n    }",
    "    if (fpu_transfer_code == 0xF02Du) {\n        i.op = Sh4Op::float_fpul;\n        i.rn = n_field(raw);\n        return i;\n    }\n"
    "    if (fpu_transfer_code == 0xF03Du) {\n        i.op = Sh4Op::ftrc;\n        i.rm = n_field(raw);\n        return i;\n    }\n"
    "    if (raw == 0xFBFDu) {\n        i.op = Sh4Op::frchg;\n        return i;\n    }",
)
replace(
    "src/core/sh4_ir.h",
    "    copy_fr_to_fpul,\n    copy_fpul_to_fr,\n    toggle_fpscr_fr,",
    "    copy_fr_to_fpul,\n    copy_fpul_to_fr,\n    convert_fpul_to_float,\n    truncate_float_to_fpul,\n    toggle_fpscr_fr,",
)
replace(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::fsts: out.op = Sh4IrOp::copy_fpul_to_fr; break;\n        case Sh4Op::frchg:",
    "        case Sh4Op::fsts: out.op = Sh4IrOp::copy_fpul_to_fr; break;\n"
    "        case Sh4Op::float_fpul: out.op = Sh4IrOp::convert_fpul_to_float; break;\n"
    "        case Sh4Op::ftrc: out.op = Sh4IrOp::truncate_float_to_fpul; break;\n"
    "        case Sh4Op::frchg:",
)
replace(
    "src/core/sh4_reference_executor.cpp",
    "#include <bit>\n#include <cstdint>\n#include <optional>",
    "#include <bit>\n#include <cmath>\n#include <cstdint>\n#include <limits>\n#include <optional>",
)
replace(
    "src/core/sh4_reference_executor.cpp",
    "constexpr std::uint32_t kFpscrFrBit = 0x00200000u;\nconstexpr std::uint32_t kFpscrSzBit = 0x00100000u;",
    "constexpr std::uint32_t kFpscrFrBit = 0x00200000u;\nconstexpr std::uint32_t kFpscrSzBit = 0x00100000u;\nconstexpr std::uint32_t kFpscrPrBit = 0x00080000u;",
)
replace(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::toggle_fpscr_fr:\n            write_fpscr(state, state.fpscr ^ kFpscrFrBit);",
    """        case Sh4IrOp::convert_fpul_to_float: {
            auto dst = require_register(instruction.dst_reg);
            if (!dst) return dst;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                return Result<void>::failure(ErrorCode::unsupported_format,
                                             \"reference FLOAT double-precision mode is not implemented\");
            }
            const auto integer = std::bit_cast<std::int32_t>(state.fpul);
            state.fr[instruction.dst_reg] =
                std::bit_cast<std::uint32_t>(static_cast<float>(integer));
            return Result<void>::success();
        }
        case Sh4IrOp::truncate_float_to_fpul: {
            auto src = require_register(instruction.src_reg);
            if (!src) return src;
            if ((state.fpscr & kFpscrPrBit) != 0u) {
                return Result<void>::failure(ErrorCode::unsupported_format,
                                             \"reference FTRC double-precision mode is not implemented\");
            }
            const auto value = std::bit_cast<float>(state.fr[instruction.src_reg]);
            const auto truncated = std::trunc(value);
            constexpr auto min_value =
                static_cast<double>(std::numeric_limits<std::int32_t>::min());
            constexpr auto max_value =
                static_cast<double>(std::numeric_limits<std::int32_t>::max());
            if (!std::isfinite(value) || static_cast<double>(truncated) < min_value ||
                static_cast<double>(truncated) > max_value) {
                return Result<void>::failure(ErrorCode::unsupported_format,
                                             \"reference FTRC exceptional conversion is not implemented\");
            }
            state.fpul =
                std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(truncated));
            return Result<void>::success();
        }
        case Sh4IrOp::toggle_fpscr_fr:
            write_fpscr(state, state.fpscr ^ kFpscrFrBit);""",
)
