from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Decoder operation model.
replace_once(
    "src/core/sh4_decoder.h",
    "    cmp_pz,\n    cmp_pl,\n    dt,\n",
    "    cmp_pz,\n    cmp_pl,\n    cmp_str_reg,\n    dt,\n    div0s,\n    div0u,\n    div1,\n",
)
replace_once(
    "src/core/sh4_decoder.h",
    "    or_imm,\n    not_reg,\n",
    "    or_imm,\n    tst_b_imm_gbr,\n    and_b_imm_gbr,\n    xor_b_imm_gbr,\n    or_b_imm_gbr,\n    not_reg,\n",
)
replace_once(
    "src/core/sh4_decoder.h",
    "    shll16,\n    shlr16,\n    fldi0,\n",
    "    shll16,\n    shlr16,\n    shal,\n    shad,\n    shld,\n    fldi0,\n",
)
replace_once(
    "src/core/sh4_decoder.h",
    "    bt_s,\n    bf_s,\n    jmp_reg,\n",
    "    bt_s,\n    bf_s,\n    braf,\n    bsrf,\n    jmp_reg,\n",
)

# Decoder patterns.
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (raw == 0x0018u) {\n        i.op = Sh4Op::sett;\n        return i;\n    }\n",
    "    if (raw == 0x0018u) {\n        i.op = Sh4Op::sett;\n        return i;\n    }\n    if (raw == 0x0019u) {\n        i.op = Sh4Op::div0u;\n        return i;\n    }\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if ((raw & 0xF00Fu) == 0x3000u) {\n        i.op = Sh4Op::cmp_eq_reg;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n",
    "    if ((raw & 0xF00Fu) == 0x3000u) {\n        i.op = Sh4Op::cmp_eq_reg;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n    if ((raw & 0xF00Fu) == 0x200Cu) {\n        i.op = Sh4Op::cmp_str_reg;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n    if ((raw & 0xF00Fu) == 0x2007u) {\n        i.op = Sh4Op::div0s;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n    if ((raw & 0xF00Fu) == 0x3004u) {\n        i.op = Sh4Op::div1;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if ((raw & 0xF00Fu) == 0x6007u) {\n",
    "    if ((raw & 0xFF00u) >= 0xCC00u && (raw & 0xFF00u) <= 0xCF00u) {\n        const auto high = static_cast<std::uint16_t>(raw & 0xFF00u);\n        if (high == 0xCC00u) i.op = Sh4Op::tst_b_imm_gbr;\n        if (high == 0xCD00u) i.op = Sh4Op::and_b_imm_gbr;\n        if (high == 0xCE00u) i.op = Sh4Op::xor_b_imm_gbr;\n        if (high == 0xCF00u) i.op = Sh4Op::or_b_imm_gbr;\n        i.immediate = static_cast<std::int32_t>(raw & 0x00FFu);\n        return i;\n    }\n    if ((raw & 0xF00Fu) == 0x6007u) {\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (shift_code == 0x4000u || shift_code == 0x4001u || shift_code == 0x4021u ||\n",
    "    if (shift_code == 0x4000u || shift_code == 0x4001u || shift_code == 0x4020u || shift_code == 0x4021u ||\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "        if (shift_code == 0x4021u) i.op = Sh4Op::shar;\n",
    "        if (shift_code == 0x4020u) i.op = Sh4Op::shal;\n        if (shift_code == 0x4021u) i.op = Sh4Op::shar;\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    const auto fpu_constant_code = static_cast<std::uint16_t>(raw & 0xF0FFu);\n",
    "    const auto dynamic_shift_code = static_cast<std::uint16_t>(raw & 0xF00Fu);\n    if (dynamic_shift_code == 0x400Cu || dynamic_shift_code == 0x400Du) {\n        i.op = dynamic_shift_code == 0x400Cu ? Sh4Op::shad : Sh4Op::shld;\n        i.rn = n_field(raw);\n        i.rm = m_field(raw);\n        return i;\n    }\n\n    const auto fpu_constant_code = static_cast<std::uint16_t>(raw & 0xF0FFu);\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if ((raw & 0xF0FFu) == 0x402Bu) {\n",
    "    if ((raw & 0xF0FFu) == 0x0023u) {\n        i.op = Sh4Op::braf;\n        i.rn = n_field(raw);\n        i.is_branch = true;\n        i.has_delay_slot = true;\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0x0003u) {\n        i.op = Sh4Op::bsrf;\n        i.rn = n_field(raw);\n        i.is_branch = true;\n        i.has_delay_slot = true;\n        i.writes_pr = true;\n        return i;\n    }\n    if ((raw & 0xF0FFu) == 0x402Bu) {\n",
)

# CFG recognizes register-relative delayed transfers as indirect control flow.
replace_once(
    "src/core/sh4_cfg.cpp",
    "        } else if (is_direct_call(i) || i.op == Sh4Op::jsr_reg) {\n",
    "        } else if (is_direct_call(i) || i.op == Sh4Op::jsr_reg || i.op == Sh4Op::bsrf) {\n",
)
replace_once(
    "src/core/sh4_cfg.cpp",
    "            if (i.op == Sh4Op::jsr_reg) {\n",
    "            if (i.op == Sh4Op::jsr_reg || i.op == Sh4Op::bsrf) {\n",
)
replace_once(
    "src/core/sh4_cfg.cpp",
    "            if (i.op == Sh4Op::jmp_reg) {\n",
    "            if (i.op == Sh4Op::jmp_reg || i.op == Sh4Op::braf) {\n",
)

# IR operation model.
replace_once(
    "src/core/sh4_ir.h",
    "    compare_pz,\n    compare_pl,\n    decrement_and_test,\n",
    "    compare_pz,\n    compare_pl,\n    compare_string_bytes,\n    decrement_and_test,\n    divide_init_signed,\n    divide_init_unsigned,\n    divide_step,\n",
)
replace_once(
    "src/core/sh4_ir.h",
    "    bit_or_imm,\n    bit_not,\n",
    "    bit_or_imm,\n    test_gbr_byte_imm,\n    and_gbr_byte_imm,\n    xor_gbr_byte_imm,\n    or_gbr_byte_imm,\n    bit_not,\n",
)
replace_once(
    "src/core/sh4_ir.h",
    "    shift_left_const,\n    shift_right_logical_const,\n    set_fr_zero,\n",
    "    shift_left_const,\n    shift_right_logical_const,\n    shift_arithmetic_dynamic,\n    shift_logical_dynamic,\n    set_fr_zero,\n",
)
replace_once(
    "src/core/sh4_ir.h",
    "    call_direct,\n    jump_reg,\n    call_reg,\n    return_pr,\n",
    "    call_direct,\n    jump_reg,\n    branch_reg_relative,\n    call_reg,\n    call_reg_relative,\n    return_pr,\n",
)

# Decoder -> IR lifting.
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::cmp_pl: out.op = Sh4IrOp::compare_pl; break;\n        case Sh4Op::dt: out.op = Sh4IrOp::decrement_and_test; break;\n",
    "        case Sh4Op::cmp_pl: out.op = Sh4IrOp::compare_pl; break;\n        case Sh4Op::cmp_str_reg: out.op = Sh4IrOp::compare_string_bytes; break;\n        case Sh4Op::dt: out.op = Sh4IrOp::decrement_and_test; break;\n        case Sh4Op::div0s: out.op = Sh4IrOp::divide_init_signed; break;\n        case Sh4Op::div0u: out.op = Sh4IrOp::divide_init_unsigned; break;\n        case Sh4Op::div1: out.op = Sh4IrOp::divide_step; break;\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::or_imm: out.op = Sh4IrOp::bit_or_imm; break;\n        case Sh4Op::not_reg: out.op = Sh4IrOp::bit_not; break;\n",
    "        case Sh4Op::or_imm: out.op = Sh4IrOp::bit_or_imm; break;\n        case Sh4Op::tst_b_imm_gbr: out.op = Sh4IrOp::test_gbr_byte_imm; break;\n        case Sh4Op::and_b_imm_gbr: out.op = Sh4IrOp::and_gbr_byte_imm; break;\n        case Sh4Op::xor_b_imm_gbr: out.op = Sh4IrOp::xor_gbr_byte_imm; break;\n        case Sh4Op::or_b_imm_gbr: out.op = Sh4IrOp::or_gbr_byte_imm; break;\n        case Sh4Op::not_reg: out.op = Sh4IrOp::bit_not; break;\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::shlr16: out.op = Sh4IrOp::shift_right_logical_const; out.imm = 16; break;\n",
    "        case Sh4Op::shlr16: out.op = Sh4IrOp::shift_right_logical_const; out.imm = 16; break;\n        case Sh4Op::shal: out.op = Sh4IrOp::shift_left_one; break;\n        case Sh4Op::shad: out.op = Sh4IrOp::shift_arithmetic_dynamic; break;\n        case Sh4Op::shld: out.op = Sh4IrOp::shift_logical_dynamic; break;\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::jmp_reg:\n            out.op = Sh4IrOp::jump_reg;\n            out.src_reg = input.rn;\n            break;\n        case Sh4Op::jsr_reg:\n            out.op = Sh4IrOp::call_reg;\n            out.src_reg = input.rn;\n            break;\n",
    "        case Sh4Op::jmp_reg:\n            out.op = Sh4IrOp::jump_reg;\n            out.src_reg = input.rn;\n            break;\n        case Sh4Op::braf:\n            out.op = Sh4IrOp::branch_reg_relative;\n            out.src_reg = input.rn;\n            break;\n        case Sh4Op::jsr_reg:\n            out.op = Sh4IrOp::call_reg;\n            out.src_reg = input.rn;\n            break;\n        case Sh4Op::bsrf:\n            out.op = Sh4IrOp::call_reg_relative;\n            out.src_reg = input.rn;\n            break;\n",
)

# Reference execution.
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "constexpr std::uint32_t kFpscrRmMask = 0x00000003u;\n",
    "constexpr std::uint32_t kFpscrRmMask = 0x00000003u;\nconstexpr std::uint32_t kSrQBit = 0x00000100u;\nconstexpr std::uint32_t kSrMBit = 0x00000200u;\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::compare_eq: {\n",
    "        case Sh4IrOp::compare_string_bytes: {\n            auto lhs = require_register(instruction.dst_reg);\n            if (!lhs) return lhs;\n            auto rhs = require_register(instruction.src_reg);\n            if (!rhs) return rhs;\n            const auto value = state.r[instruction.dst_reg] ^ state.r[instruction.src_reg];\n            state.t = (value & 0x000000FFu) == 0u ||\n                      (value & 0x0000FF00u) == 0u ||\n                      (value & 0x00FF0000u) == 0u ||\n                      (value & 0xFF000000u) == 0u;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::divide_init_unsigned:\n            state.sr &= ~(kSrQBit | kSrMBit);\n            state.t = false;\n            return Result<void>::success();\n        case Sh4IrOp::divide_init_signed: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            const bool q = (state.r[instruction.dst_reg] & 0x80000000u) != 0u;\n            const bool m = (state.r[instruction.src_reg] & 0x80000000u) != 0u;\n            state.sr = (state.sr & ~(kSrQBit | kSrMBit)) |\n                       (q ? kSrQBit : 0u) | (m ? kSrMBit : 0u);\n            state.t = q != m;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::divide_step: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            bool old_q = (state.sr & kSrQBit) != 0u;\n            const bool m = (state.sr & kSrMBit) != 0u;\n            bool q = (state.r[instruction.dst_reg] & 0x80000000u) != 0u;\n            auto& rn = state.r[instruction.dst_reg];\n            const auto rm = state.r[instruction.src_reg];\n            rn = (rn << 1u) | (state.t ? 1u : 0u);\n            std::uint32_t before{};\n            bool carry_or_borrow{};\n            if (!old_q && !m) {\n                before = rn; rn -= rm; carry_or_borrow = rn > before;\n                q = q ? !carry_or_borrow : carry_or_borrow;\n            } else if (!old_q && m) {\n                before = rn; rn += rm; carry_or_borrow = rn < before;\n                q = q ? carry_or_borrow : !carry_or_borrow;\n            } else if (old_q && !m) {\n                before = rn; rn += rm; carry_or_borrow = rn < before;\n                q = q ? !carry_or_borrow : carry_or_borrow;\n            } else {\n                before = rn; rn -= rm; carry_or_borrow = rn > before;\n                q = q ? carry_or_borrow : !carry_or_borrow;\n            }\n            if (q) state.sr |= kSrQBit; else state.sr &= ~kSrQBit;\n            state.t = q == m;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::compare_eq: {\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::bit_not:\n",
    "        case Sh4IrOp::test_gbr_byte_imm:\n        case Sh4IrOp::and_gbr_byte_imm:\n        case Sh4IrOp::xor_gbr_byte_imm:\n        case Sh4IrOp::or_gbr_byte_imm: {\n            const auto address = state.gbr + state.r[0];\n            auto loaded = read_u8(memory, address);\n            if (!loaded) return Result<void>::failure(loaded.error, loaded.detail);\n            const auto immediate = static_cast<std::uint8_t>(instruction.imm & 0xFF);\n            if (instruction.op == Sh4IrOp::test_gbr_byte_imm) {\n                state.t = (loaded.value & immediate) == 0u;\n                return Result<void>::success();\n            }\n            std::uint8_t result = loaded.value;\n            if (instruction.op == Sh4IrOp::and_gbr_byte_imm) result &= immediate;\n            else if (instruction.op == Sh4IrOp::xor_gbr_byte_imm) result ^= immediate;\n            else result |= immediate;\n            return write_u8(memory, address, result);\n        }\n        case Sh4IrOp::bit_not:\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::shift_left_one:\n",
    "        case Sh4IrOp::shift_arithmetic_dynamic:\n        case Sh4IrOp::shift_logical_dynamic: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            const auto count_value = state.r[instruction.src_reg];\n            const auto value = state.r[instruction.dst_reg];\n            if ((count_value & 0x80000000u) == 0u) {\n                state.r[instruction.dst_reg] = value << (count_value & 31u);\n            } else {\n                const auto count = static_cast<unsigned>(((~count_value) & 31u) + 1u);\n                if (instruction.op == Sh4IrOp::shift_logical_dynamic) {\n                    state.r[instruction.dst_reg] = count >= 32u ? 0u : value >> count;\n                } else if (count >= 32u) {\n                    state.r[instruction.dst_reg] = (value & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u;\n                } else {\n                    auto shifted = value >> count;\n                    if ((value & 0x80000000u) != 0u) shifted |= (~0u << (32u - count));\n                    state.r[instruction.dst_reg] = shifted;\n                }\n            }\n            return Result<void>::success();\n        }\n        case Sh4IrOp::shift_left_one:\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::jump_reg: {\n",
    "        case Sh4IrOp::branch_reg_relative: {\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            pending = PendingTransfer{instruction.source_address + 4u + state.r[instruction.src_reg], std::nullopt};\n            return Result<void>::success();\n        }\n        case Sh4IrOp::jump_reg: {\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::return_pr:\n",
    "        case Sh4IrOp::call_reg_relative: {\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            const auto target = instruction.source_address + 4u + state.r[instruction.src_reg];\n            state.pr = instruction.source_address + 4u;\n            pending = PendingTransfer{target, std::nullopt};\n            return Result<void>::success();\n        }\n        case Sh4IrOp::return_pr:\n",
)

# End-to-end scalar behavior tests. Decoder/GBR/CFG RED commits already establish the failing state.
replace_once(
    "tests/test_sh4_integer_pipeline.cpp",
    "int main() {\n    test_decoder_patterns();\n",
    "static void test_cmpstr_division_dynamic_shifts_and_relative_branches() {\n    constexpr std::uint32_t sr_q = 0x00000100u;\n    constexpr std::uint32_t sr_m = 0x00000200u;\n\n    {\n        jojo::Sh4ReferenceState state{};\n        state.r[1] = 0x11223344u; state.r[2] = 0x9922AA88u; state.pr = 0xDEAD3000u;\n        CHECK(execute_words({0x212Cu, 0x000Bu, 0x0009u}, state, 0x8C013000u));\n        CHECK(state.t);\n    }\n    {\n        jojo::Sh4ReferenceState state{};\n        state.r[1] = 0x80000000u; state.r[2] = 1u; state.pr = 0xDEAD3100u;\n        CHECK(execute_words({0x2127u, 0x000Bu, 0x0009u}, state, 0x8C013100u));\n        CHECK((state.sr & sr_q) != 0u && (state.sr & sr_m) == 0u && state.t);\n        state.pr = 0xDEAD3100u;\n        CHECK(execute_words({0x0019u, 0x000Bu, 0x0009u}, state, 0x8C013100u));\n        CHECK((state.sr & (sr_q | sr_m)) == 0u && !state.t);\n    }\n    {\n        jojo::Sh4ReferenceState state{};\n        state.r[1] = 5u; state.r[2] = 2u; state.t = false; state.pr = 0xDEAD3200u;\n        CHECK(execute_words({0x3124u, 0x000Bu, 0x0009u}, state, 0x8C013200u));\n        CHECK(state.r[1] == 8u && (state.sr & (sr_q | sr_m)) == 0u && state.t);\n    }\n    {\n        jojo::Sh4ReferenceState state{};\n        state.r[1] = 0x80000001u; state.r[2] = 0xFFFFFFFFu; state.pr = 0xDEAD3300u;\n        CHECK(execute_words({0x412Cu, 0x000Bu, 0x0009u}, state, 0x8C013300u));\n        CHECK(state.r[1] == 0xC0000000u);\n        state.r[1] = 0x80000001u; state.pr = 0xDEAD3300u;\n        CHECK(execute_words({0x412Du, 0x000Bu, 0x0009u}, state, 0x8C013300u));\n        CHECK(state.r[1] == 0x40000000u);\n        state.r[1] = 0x80000001u; state.pr = 0xDEAD3300u;\n        CHECK(execute_words({0x4120u, 0x000Bu, 0x0009u}, state, 0x8C013300u));\n        CHECK(state.r[1] == 2u && state.t);\n    }\n    {\n        jojo::Sh4ReferenceState state{};\n        state.r[3] = 0x100u;\n        CHECK(execute_words({0x0323u, 0x0009u}, state, 0x8C013400u));\n        CHECK(state.pc == 0x8C013504u);\n        state.r[4] = 0x200u;\n        CHECK(execute_words({0x0403u, 0x0009u}, state, 0x8C013500u));\n        CHECK(state.pr == 0x8C013504u && state.pc == 0x8C013704u);\n    }\n}\n\nint main() {\n    test_decoder_patterns();\n",
)
replace_once(
    "tests/test_sh4_integer_pipeline.cpp",
    "    test_shift_semantics_and_t_bit();\n    if (failures) {\n",
    "    test_shift_semantics_and_t_bit();\n    test_cmpstr_division_dynamic_shifts_and_relative_branches();\n    if (failures) {\n",
)
