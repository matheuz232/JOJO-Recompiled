from pathlib import Path

path = Path("src/core/sh4_reference_executor.cpp")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one match, got {count}: {old[:100]!r}")
    text = text.replace(old, new, 1)


replace_once(
    "            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(sine);\n            state.fr[instruction.dst_reg + 1u] = std::bit_cast<std::uint32_t>(cosine);\n            return Result<void>::success();",
    "            if (!apply_fpu_cause(instruction, state, pending, kFpscrCauseI)) return Result<void>::success();\n            state.fr[instruction.dst_reg] = std::bit_cast<std::uint32_t>(sine);\n            state.fr[instruction.dst_reg + 1u] = std::bit_cast<std::uint32_t>(cosine);\n            return Result<void>::success();",
)

replace_once(
    "            const auto value = normalize_single(state, state.fr[instruction.dst_reg]);\n            std::uint32_t cause{};\n            float result{};\n            if (std::isnan(value) || value < 0.0f) {",
    "            const auto operand_bits = state.fr[instruction.dst_reg];\n            if ((state.fpscr & kFpscrDnBit) == 0u && is_single_subnormal(operand_bits)) {\n                apply_fpu_cause(instruction, state, pending, kFpscrCauseE);\n                return Result<void>::success();\n            }\n            const auto value = normalize_single(state, operand_bits);\n            std::uint32_t cause{};\n            float result{};\n            if (std::isnan(value) || value < 0.0f) {",
)

path.write_text(text)
Path(__file__).unlink()
