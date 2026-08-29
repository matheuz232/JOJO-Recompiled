from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# ---- Decoder operation model -------------------------------------------------
replace_once(
    "src/core/sh4_decoder.h",
    "    rte,\n    clrt,\n    sett,\n",
    "    rte,\n    clrs,\n    sets,\n    clrt,\n    sett,\n    ldtlb,\n    sleep,\n",
)
replace_once(
    "src/core/sh4_decoder.h",
    "    stc_gbr_predec,\n    lds_mach_reg,\n",
    "    stc_gbr_predec,\n    ldc_sr_reg,\n    ldc_vbr_reg,\n    ldc_ssr_reg,\n    ldc_spc_reg,\n    ldc_dbr_reg,\n    stc_sr_reg,\n    stc_vbr_reg,\n    stc_ssr_reg,\n    stc_spc_reg,\n    stc_sgr_reg,\n    stc_dbr_reg,\n    ldc_sr_postinc,\n    ldc_vbr_postinc,\n    ldc_ssr_postinc,\n    ldc_spc_postinc,\n    ldc_dbr_postinc,\n    stc_sr_predec,\n    stc_vbr_predec,\n    stc_ssr_predec,\n    stc_spc_predec,\n    stc_sgr_predec,\n    stc_dbr_predec,\n    ldc_bank_reg,\n    stc_bank_reg,\n    ldc_bank_postinc,\n    stc_bank_predec,\n    lds_mach_reg,\n",
)
replace_once(
    "src/core/sh4_decoder.h",
    "    dmuls_l,\n    dmulu_l,\n",
    "    dmuls_l,\n    dmulu_l,\n    mac_l,\n    mac_w,\n",
)
replace_once(
    "src/core/sh4_decoder.h",
    "    shad,\n    shld,\n    fldi0,\n",
    "    shad,\n    shld,\n    tas_b,\n    trapa,\n    movca_l,\n    ocbi,\n    ocbp,\n    ocbwb,\n    pref,\n    fldi0,\n",
)

# ---- Decoder patterns --------------------------------------------------------
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (raw == 0x0008u) {\n        i.op = Sh4Op::clrt;\n        return i;\n    }\n",
    "    if (raw == 0x0048u) { i.op = Sh4Op::clrs; return i; }\n    if (raw == 0x0058u) { i.op = Sh4Op::sets; return i; }\n    if (raw == 0x0038u) { i.op = Sh4Op::ldtlb; return i; }\n    if (raw == 0x001Bu) { i.op = Sh4Op::sleep; return i; }\n    if (raw == 0x0008u) {\n        i.op = Sh4Op::clrt;\n        return i;\n    }\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    const auto control_code = static_cast<std::uint16_t>(raw & 0xF0FFu);\n    if (control_code == 0x401Eu) { i.op = Sh4Op::ldc_gbr_reg; i.rm = n_field(raw); return i; }\n",
    "    const auto control_code = static_cast<std::uint16_t>(raw & 0xF0FFu);\n\n    if ((raw & 0xF08Fu) == 0x408Eu) {\n        i.op = Sh4Op::ldc_bank_reg;\n        i.rm = n_field(raw);\n        i.rn = static_cast<std::uint8_t>((raw >> 4u) & 0x7u);\n        return i;\n    }\n    if ((raw & 0xF08Fu) == 0x0082u) {\n        i.op = Sh4Op::stc_bank_reg;\n        i.rn = n_field(raw);\n        i.rm = static_cast<std::uint8_t>((raw >> 4u) & 0x7u);\n        return i;\n    }\n    if ((raw & 0xF08Fu) == 0x4087u) {\n        i.op = Sh4Op::ldc_bank_postinc;\n        i.rm = n_field(raw);\n        i.rn = static_cast<std::uint8_t>((raw >> 4u) & 0x7u);\n        return i;\n    }\n    if ((raw & 0xF08Fu) == 0x4083u) {\n        i.op = Sh4Op::stc_bank_predec;\n        i.rn = n_field(raw);\n        i.rm = static_cast<std::uint8_t>((raw >> 4u) & 0x7u);\n        return i;\n    }\n\n    if (control_code == 0x400Eu) { i.op = Sh4Op::ldc_sr_reg; i.rm = n_field(raw); return i; }\n    if (control_code == 0x402Eu) { i.op = Sh4Op::ldc_vbr_reg; i.rm = n_field(raw); return i; }\n    if (control_code == 0x403Eu) { i.op = Sh4Op::ldc_ssr_reg; i.rm = n_field(raw); return i; }\n    if (control_code == 0x404Eu) { i.op = Sh4Op::ldc_spc_reg; i.rm = n_field(raw); return i; }\n    if (control_code == 0x40FAu) { i.op = Sh4Op::ldc_dbr_reg; i.rm = n_field(raw); return i; }\n    if (control_code == 0x0002u) { i.op = Sh4Op::stc_sr_reg; i.rn = n_field(raw); return i; }\n    if (control_code == 0x0022u) { i.op = Sh4Op::stc_vbr_reg; i.rn = n_field(raw); return i; }\n    if (control_code == 0x0032u) { i.op = Sh4Op::stc_ssr_reg; i.rn = n_field(raw); return i; }\n    if (control_code == 0x0042u) { i.op = Sh4Op::stc_spc_reg; i.rn = n_field(raw); return i; }\n    if (control_code == 0x003Au) { i.op = Sh4Op::stc_sgr_reg; i.rn = n_field(raw); return i; }\n    if (control_code == 0x00FAu) { i.op = Sh4Op::stc_dbr_reg; i.rn = n_field(raw); return i; }\n    if (control_code == 0x4007u) { i.op = Sh4Op::ldc_sr_postinc; i.rm = n_field(raw); return i; }\n    if (control_code == 0x4027u) { i.op = Sh4Op::ldc_vbr_postinc; i.rm = n_field(raw); return i; }\n    if (control_code == 0x4037u) { i.op = Sh4Op::ldc_ssr_postinc; i.rm = n_field(raw); return i; }\n    if (control_code == 0x4047u) { i.op = Sh4Op::ldc_spc_postinc; i.rm = n_field(raw); return i; }\n    if (control_code == 0x40F6u) { i.op = Sh4Op::ldc_dbr_postinc; i.rm = n_field(raw); return i; }\n    if (control_code == 0x4003u) { i.op = Sh4Op::stc_sr_predec; i.rn = n_field(raw); return i; }\n    if (control_code == 0x4023u) { i.op = Sh4Op::stc_vbr_predec; i.rn = n_field(raw); return i; }\n    if (control_code == 0x4033u) { i.op = Sh4Op::stc_ssr_predec; i.rn = n_field(raw); return i; }\n    if (control_code == 0x4043u) { i.op = Sh4Op::stc_spc_predec; i.rn = n_field(raw); return i; }\n    if (control_code == 0x4032u) { i.op = Sh4Op::stc_sgr_predec; i.rn = n_field(raw); return i; }\n    if (control_code == 0x40F2u) { i.op = Sh4Op::stc_dbr_predec; i.rn = n_field(raw); return i; }\n\n    if (control_code == 0x401Eu) { i.op = Sh4Op::ldc_gbr_reg; i.rm = n_field(raw); return i; }\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (raw == 0x0028u) {\n        i.op = Sh4Op::clrmac;\n        return i;\n    }\n",
    "    if (raw == 0x0028u) {\n        i.op = Sh4Op::clrmac;\n        return i;\n    }\n\n    if (control_code == 0x401Bu) { i.op = Sh4Op::tas_b; i.rn = n_field(raw); return i; }\n    if (control_code == 0x00C3u) { i.op = Sh4Op::movca_l; i.rn = n_field(raw); return i; }\n    if (control_code == 0x0093u) { i.op = Sh4Op::ocbi; i.rn = n_field(raw); return i; }\n    if (control_code == 0x00A3u) { i.op = Sh4Op::ocbp; i.rn = n_field(raw); return i; }\n    if (control_code == 0x00B3u) { i.op = Sh4Op::ocbwb; i.rn = n_field(raw); return i; }\n    if (control_code == 0x0083u) { i.op = Sh4Op::pref; i.rn = n_field(raw); return i; }\n    if ((raw & 0xFF00u) == 0xC300u) {\n        i.op = Sh4Op::trapa;\n        i.immediate = static_cast<std::int32_t>(raw & 0x00FFu);\n        return i;\n    }\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "    if (multiply_code == 0x0007u || multiply_code == 0x200Fu ||\n        multiply_code == 0x200Eu || multiply_code == 0x300Du ||\n        multiply_code == 0x3005u) {\n",
    "    if (multiply_code == 0x0007u || multiply_code == 0x200Fu ||\n        multiply_code == 0x200Eu || multiply_code == 0x300Du ||\n        multiply_code == 0x3005u || multiply_code == 0x000Fu ||\n        multiply_code == 0x400Fu) {\n",
)
replace_once(
    "src/core/sh4_decoder.cpp",
    "        else if (multiply_code == 0x300Du) i.op = Sh4Op::dmuls_l;\n        else i.op = Sh4Op::dmulu_l;\n",
    "        else if (multiply_code == 0x300Du) i.op = Sh4Op::dmuls_l;\n        else if (multiply_code == 0x3005u) i.op = Sh4Op::dmulu_l;\n        else if (multiply_code == 0x000Fu) i.op = Sh4Op::mac_l;\n        else i.op = Sh4Op::mac_w;\n",
)

# ---- IR operation model ------------------------------------------------------
replace_once(
    "src/core/sh4_ir.h",
    "    nop,\n    clear_t,\n    set_t,\n",
    "    nop,\n    clear_s,\n    set_s,\n    clear_t,\n    set_t,\n    ldtlb_event,\n    sleep_cpu,\n",
)
replace_once(
    "src/core/sh4_ir.h",
    "    store_gbr_predec32,\n    set_mach_from_reg,\n",
    "    store_gbr_predec32,\n    set_control_from_reg,\n    copy_control_to_reg,\n    load_control_postinc32,\n    store_control_predec32,\n    set_bank_from_reg,\n    copy_bank_to_reg,\n    load_bank_postinc32,\n    store_bank_predec32,\n    set_mach_from_reg,\n",
)
replace_once(
    "src/core/sh4_ir.h",
    "    multiply_signed_long,\n    multiply_unsigned_long,\n",
    "    multiply_signed_long,\n    multiply_unsigned_long,\n    multiply_accumulate_long,\n    multiply_accumulate_word,\n",
)
replace_once(
    "src/core/sh4_ir.h",
    "    shift_arithmetic_dynamic,\n    shift_logical_dynamic,\n    set_fr_zero,\n",
    "    shift_arithmetic_dynamic,\n    shift_logical_dynamic,\n    test_and_set_byte,\n    trap_imm,\n    movca_long,\n    ocbi_event,\n    ocbp_event,\n    ocbwb_event,\n    pref_event,\n    set_fr_zero,\n",
)

# ---- Decoder -> IR lifting ---------------------------------------------------
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::nop: out.op = Sh4IrOp::nop; break;\n        case Sh4Op::clrt: out.op = Sh4IrOp::clear_t; break;\n",
    "        case Sh4Op::nop: out.op = Sh4IrOp::nop; break;\n        case Sh4Op::clrs: out.op = Sh4IrOp::clear_s; break;\n        case Sh4Op::sets: out.op = Sh4IrOp::set_s; break;\n        case Sh4Op::clrt: out.op = Sh4IrOp::clear_t; break;\n        case Sh4Op::ldtlb: out.op = Sh4IrOp::ldtlb_event; break;\n        case Sh4Op::sleep: out.op = Sh4IrOp::sleep_cpu; break;\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::stc_gbr_predec: out.op = Sh4IrOp::store_gbr_predec32; break;\n        case Sh4Op::lds_mach_reg: out.op = Sh4IrOp::set_mach_from_reg; break;\n",
    "        case Sh4Op::stc_gbr_predec: out.op = Sh4IrOp::store_gbr_predec32; break;\n        case Sh4Op::ldc_sr_reg: out.op = Sh4IrOp::set_control_from_reg; out.imm = 0; break;\n        case Sh4Op::ldc_vbr_reg: out.op = Sh4IrOp::set_control_from_reg; out.imm = 1; break;\n        case Sh4Op::ldc_ssr_reg: out.op = Sh4IrOp::set_control_from_reg; out.imm = 2; break;\n        case Sh4Op::ldc_spc_reg: out.op = Sh4IrOp::set_control_from_reg; out.imm = 3; break;\n        case Sh4Op::ldc_dbr_reg: out.op = Sh4IrOp::set_control_from_reg; out.imm = 5; break;\n        case Sh4Op::stc_sr_reg: out.op = Sh4IrOp::copy_control_to_reg; out.imm = 0; break;\n        case Sh4Op::stc_vbr_reg: out.op = Sh4IrOp::copy_control_to_reg; out.imm = 1; break;\n        case Sh4Op::stc_ssr_reg: out.op = Sh4IrOp::copy_control_to_reg; out.imm = 2; break;\n        case Sh4Op::stc_spc_reg: out.op = Sh4IrOp::copy_control_to_reg; out.imm = 3; break;\n        case Sh4Op::stc_sgr_reg: out.op = Sh4IrOp::copy_control_to_reg; out.imm = 4; break;\n        case Sh4Op::stc_dbr_reg: out.op = Sh4IrOp::copy_control_to_reg; out.imm = 5; break;\n        case Sh4Op::ldc_sr_postinc: out.op = Sh4IrOp::load_control_postinc32; out.imm = 0; break;\n        case Sh4Op::ldc_vbr_postinc: out.op = Sh4IrOp::load_control_postinc32; out.imm = 1; break;\n        case Sh4Op::ldc_ssr_postinc: out.op = Sh4IrOp::load_control_postinc32; out.imm = 2; break;\n        case Sh4Op::ldc_spc_postinc: out.op = Sh4IrOp::load_control_postinc32; out.imm = 3; break;\n        case Sh4Op::ldc_dbr_postinc: out.op = Sh4IrOp::load_control_postinc32; out.imm = 5; break;\n        case Sh4Op::stc_sr_predec: out.op = Sh4IrOp::store_control_predec32; out.imm = 0; break;\n        case Sh4Op::stc_vbr_predec: out.op = Sh4IrOp::store_control_predec32; out.imm = 1; break;\n        case Sh4Op::stc_ssr_predec: out.op = Sh4IrOp::store_control_predec32; out.imm = 2; break;\n        case Sh4Op::stc_spc_predec: out.op = Sh4IrOp::store_control_predec32; out.imm = 3; break;\n        case Sh4Op::stc_sgr_predec: out.op = Sh4IrOp::store_control_predec32; out.imm = 4; break;\n        case Sh4Op::stc_dbr_predec: out.op = Sh4IrOp::store_control_predec32; out.imm = 5; break;\n        case Sh4Op::ldc_bank_reg: out.op = Sh4IrOp::set_bank_from_reg; break;\n        case Sh4Op::stc_bank_reg: out.op = Sh4IrOp::copy_bank_to_reg; break;\n        case Sh4Op::ldc_bank_postinc: out.op = Sh4IrOp::load_bank_postinc32; break;\n        case Sh4Op::stc_bank_predec: out.op = Sh4IrOp::store_bank_predec32; break;\n        case Sh4Op::lds_mach_reg: out.op = Sh4IrOp::set_mach_from_reg; break;\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::dmulu_l: out.op = Sh4IrOp::multiply_unsigned_long; break;\n        case Sh4Op::exts_b: out.op = Sh4IrOp::sign_extend_byte; break;\n",
    "        case Sh4Op::dmulu_l: out.op = Sh4IrOp::multiply_unsigned_long; break;\n        case Sh4Op::mac_l: out.op = Sh4IrOp::multiply_accumulate_long; break;\n        case Sh4Op::mac_w: out.op = Sh4IrOp::multiply_accumulate_word; break;\n        case Sh4Op::exts_b: out.op = Sh4IrOp::sign_extend_byte; break;\n",
)
replace_once(
    "src/core/sh4_ir.cpp",
    "        case Sh4Op::shld: out.op = Sh4IrOp::shift_logical_dynamic; break;\n        case Sh4Op::fldi0: out.op = Sh4IrOp::set_fr_zero; break;\n",
    "        case Sh4Op::shld: out.op = Sh4IrOp::shift_logical_dynamic; break;\n        case Sh4Op::tas_b: out.op = Sh4IrOp::test_and_set_byte; break;\n        case Sh4Op::trapa: out.op = Sh4IrOp::trap_imm; break;\n        case Sh4Op::movca_l: out.op = Sh4IrOp::movca_long; break;\n        case Sh4Op::ocbi: out.op = Sh4IrOp::ocbi_event; break;\n        case Sh4Op::ocbp: out.op = Sh4IrOp::ocbp_event; break;\n        case Sh4Op::ocbwb: out.op = Sh4IrOp::ocbwb_event; break;\n        case Sh4Op::pref: out.op = Sh4IrOp::pref_event; break;\n        case Sh4Op::fldi0: out.op = Sh4IrOp::set_fr_zero; break;\n",
)

# ---- Reference state ---------------------------------------------------------
replace_once(
    "src/core/sh4_reference_executor.h",
    "#include <string>\n#include <vector>\n",
    "#include <string>\n#include <utility>\n#include <vector>\n",
)
replace_once(
    "src/core/sh4_reference_executor.h",
    "enum class Sh4ReferenceStopReason {\n    left_program,\n    end_of_stream,\n    block_limit,\n};\n",
    "enum class Sh4ReferenceStopReason {\n    left_program,\n    end_of_stream,\n    block_limit,\n    sleep,\n};\n\nenum class Sh4ReferenceSystemEvent {\n    none,\n    ldtlb,\n    movca_l,\n    ocbi,\n    ocbp,\n    ocbwb,\n    pref,\n    sleep,\n};\n",
)
replace_once(
    "src/core/sh4_reference_executor.h",
    "    std::array<std::uint32_t, 16> r{};\n    std::array<std::uint32_t, 16> fr{};\n",
    "    std::array<std::uint32_t, 16> r{};\n    std::array<std::uint32_t, 8> r_bank{};\n    std::array<std::uint32_t, 16> fr{};\n",
)
replace_once(
    "src/core/sh4_reference_executor.h",
    "    std::uint32_t sgr{};\n    std::uint32_t vbr{};\n    std::uint32_t intevt{};\n    bool t{};\n};\n",
    "    std::uint32_t sgr{};\n    std::uint32_t vbr{};\n    std::uint32_t dbr{};\n    std::uint32_t tra{};\n    std::uint32_t expevt{};\n    std::uint32_t intevt{};\n    bool t{};\n    bool sleeping{};\n    Sh4ReferenceSystemEvent last_system_event{Sh4ReferenceSystemEvent::none};\n    std::uint32_t system_event_address{};\n};\n\ninline std::uint32_t read_sh4_reference_sr(const Sh4ReferenceState& state) noexcept {\n    return (state.sr & ~1u) | (state.t ? 1u : 0u);\n}\n\ninline void write_sh4_reference_sr(Sh4ReferenceState& state, std::uint32_t value) noexcept {\n    constexpr std::uint32_t md = 0x40000000u;\n    constexpr std::uint32_t rb = 0x20000000u;\n    const bool old_bank_one = (state.sr & (md | rb)) == (md | rb);\n    const bool new_bank_one = (value & (md | rb)) == (md | rb);\n    if (old_bank_one != new_bank_one) {\n        for (std::size_t index = 0; index < state.r_bank.size(); ++index) {\n            std::swap(state.r[index], state.r_bank[index]);\n        }\n    }\n    state.sr = value & ~1u;\n    state.t = (value & 1u) != 0u;\n}\n",
)

# ---- Interrupt entry uses composite SR and bank switching --------------------
replace_once(
    "src/core/sh4_interrupt_entry.cpp",
    "    state.ssr = state.sr;\n",
    "    state.ssr = read_sh4_reference_sr(state);\n",
)
replace_once(
    "src/core/sh4_interrupt_entry.cpp",
    "    state.sr |= kSrMd | kSrRb | kSrBl;\n",
    "    write_sh4_reference_sr(state, read_sh4_reference_sr(state) | kSrMd | kSrRb | kSrBl);\n",
)

# ---- Reference executor helpers ----------------------------------------------
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "constexpr std::uint32_t kSrMBit = 0x00000200u;\n",
    "constexpr std::uint32_t kSrMBit = 0x00000200u;\nconstexpr std::uint32_t kSrSBit = 0x00000002u;\nconstexpr std::uint32_t kSrBlBit = 0x10000000u;\nconstexpr std::uint32_t kSrRbBit = 0x20000000u;\nconstexpr std::uint32_t kSrMdBit = 0x40000000u;\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "struct PendingTransfer {\n    std::uint32_t target{};\n    std::optional<bool> condition;\n};\n\nResult<void> execute_op",
    "struct PendingTransfer {\n    std::uint32_t target{};\n    std::optional<bool> condition;\n    bool immediate{};\n};\n\nvoid enter_general_exception(const Sh4IrInstruction& instruction,\n                             Sh4ReferenceState& state,\n                             std::optional<PendingTransfer>& pending,\n                             std::uint32_t event_code) {\n    state.spc = instruction.in_delay_slot\n        ? instruction.source_address - 2u\n        : instruction.source_address;\n    state.ssr = read_sh4_reference_sr(state);\n    state.sgr = state.r[15];\n    state.expevt = event_code;\n    write_sh4_reference_sr(state, state.ssr | kSrMdBit | kSrRbBit | kSrBlBit);\n    pending = PendingTransfer{state.vbr + 0x100u, std::nullopt, true};\n}\n\nbool require_privileged(const Sh4IrInstruction& instruction,\n                        Sh4ReferenceState& state,\n                        std::optional<PendingTransfer>& pending) {\n    if ((state.sr & kSrMdBit) != 0u) return true;\n    enter_general_exception(instruction, state, pending,\n                            instruction.in_delay_slot ? 0x1A0u : 0x180u);\n    return false;\n}\n\nstd::uint32_t control_value(const Sh4ReferenceState& state, std::int32_t selector) {\n    switch (selector) {\n        case 0: return read_sh4_reference_sr(state);\n        case 1: return state.vbr;\n        case 2: return state.ssr;\n        case 3: return state.spc;\n        case 4: return state.sgr;\n        case 5: return state.dbr;\n        default: return 0u;\n    }\n}\n\nvoid set_control_value(Sh4ReferenceState& state, std::int32_t selector, std::uint32_t value) {\n    switch (selector) {\n        case 0: write_sh4_reference_sr(state, value); break;\n        case 1: state.vbr = value; break;\n        case 2: state.ssr = value; break;\n        case 3: state.spc = value; break;\n        case 5: state.dbr = value; break;\n        default: break;\n    }\n}\n\nResult<void> execute_op",
)

# ---- Reference executor instruction semantics --------------------------------
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::nop:\n            return Result<void>::success();\n        case Sh4IrOp::clear_t:\n",
    "        case Sh4IrOp::nop:\n            return Result<void>::success();\n        case Sh4IrOp::clear_s:\n            state.sr &= ~kSrSBit;\n            return Result<void>::success();\n        case Sh4IrOp::set_s:\n            state.sr |= kSrSBit;\n            return Result<void>::success();\n        case Sh4IrOp::ldtlb_event:\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            state.last_system_event = Sh4ReferenceSystemEvent::ldtlb;\n            state.system_event_address = 0u;\n            return Result<void>::success();\n        case Sh4IrOp::sleep_cpu:\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            state.sleeping = true;\n            state.last_system_event = Sh4ReferenceSystemEvent::sleep;\n            state.system_event_address = 0u;\n            return Result<void>::success();\n        case Sh4IrOp::clear_t:\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::set_mach_from_reg: {\n",
    "        case Sh4IrOp::set_control_from_reg: {\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            if (instruction.imm == 0 && instruction.in_delay_slot) {\n                enter_general_exception(instruction, state, pending, 0x1A0u);\n                return Result<void>::success();\n            }\n            const auto value = state.r[instruction.src_reg];\n            set_control_value(state, instruction.imm, value);\n            return Result<void>::success();\n        }\n        case Sh4IrOp::copy_control_to_reg: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            state.r[instruction.dst_reg] = control_value(state, instruction.imm);\n            return Result<void>::success();\n        }\n        case Sh4IrOp::load_control_postinc32: {\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            if (instruction.imm == 0 && instruction.in_delay_slot) {\n                enter_general_exception(instruction, state, pending, 0x1A0u);\n                return Result<void>::success();\n            }\n            const auto address = state.r[instruction.src_reg];\n            auto value = read_u32(memory, address);\n            if (!value) return Result<void>::failure(value.error, value.detail);\n            state.r[instruction.src_reg] = address + 4u;\n            set_control_value(state, instruction.imm, value.value);\n            return Result<void>::success();\n        }\n        case Sh4IrOp::store_control_predec32: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            const auto address = state.r[instruction.dst_reg] - 4u;\n            auto stored = write_u32(memory, address, control_value(state, instruction.imm));\n            if (!stored) return stored;\n            state.r[instruction.dst_reg] = address;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::set_bank_from_reg: {\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            if (instruction.dst_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, \"SH-4 bank index is out of range\");\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            state.r_bank[instruction.dst_reg] = state.r[instruction.src_reg];\n            return Result<void>::success();\n        }\n        case Sh4IrOp::copy_bank_to_reg: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            if (instruction.src_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, \"SH-4 bank index is out of range\");\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            state.r[instruction.dst_reg] = state.r_bank[instruction.src_reg];\n            return Result<void>::success();\n        }\n        case Sh4IrOp::load_bank_postinc32: {\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            if (instruction.dst_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, \"SH-4 bank index is out of range\");\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            const auto address = state.r[instruction.src_reg];\n            auto value = read_u32(memory, address);\n            if (!value) return Result<void>::failure(value.error, value.detail);\n            state.r[instruction.src_reg] = address + 4u;\n            state.r_bank[instruction.dst_reg] = value.value;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::store_bank_predec32: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            if (instruction.src_reg >= 8u) return Result<void>::failure(ErrorCode::invalid_argument, \"SH-4 bank index is out of range\");\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            const auto address = state.r[instruction.dst_reg] - 4u;\n            auto stored = write_u32(memory, address, state.r_bank[instruction.src_reg]);\n            if (!stored) return stored;\n            state.r[instruction.dst_reg] = address;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::set_mach_from_reg: {\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::multiply_unsigned_long: {\n",
    "        case Sh4IrOp::multiply_accumulate_long: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            const auto dst_address = state.r[instruction.dst_reg];\n            const auto src_address = state.r[instruction.src_reg];\n            auto lhs_raw = read_u32(memory, src_address);\n            if (!lhs_raw) return Result<void>::failure(lhs_raw.error, lhs_raw.detail);\n            auto rhs_raw = read_u32(memory, dst_address);\n            if (!rhs_raw) return Result<void>::failure(rhs_raw.error, rhs_raw.detail);\n            const auto lhs = std::bit_cast<std::int32_t>(lhs_raw.value);\n            const auto rhs = std::bit_cast<std::int32_t>(rhs_raw.value);\n            const auto product = static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs);\n            const std::uint64_t combined = (static_cast<std::uint64_t>(state.mach) << 32u) | state.macl;\n            std::uint64_t result_bits{};\n            if ((state.sr & kSrSBit) == 0u) {\n                result_bits = combined + static_cast<std::uint64_t>(product);\n            } else {\n                constexpr std::int64_t min48 = -(static_cast<std::int64_t>(1) << 47u);\n                constexpr std::int64_t max48 = (static_cast<std::int64_t>(1) << 47u) - 1;\n                const auto accumulator = std::bit_cast<std::int64_t>(combined);\n                std::int64_t sum{};\n                if (product > 0 && accumulator > std::numeric_limits<std::int64_t>::max() - product) sum = max48;\n                else if (product < 0 && accumulator < std::numeric_limits<std::int64_t>::min() - product) sum = min48;\n                else {\n                    sum = accumulator + product;\n                    if (sum < min48) sum = min48;\n                    if (sum > max48) sum = max48;\n                }\n                result_bits = std::bit_cast<std::uint64_t>(sum);\n            }\n            state.macl = static_cast<std::uint32_t>(result_bits);\n            state.mach = static_cast<std::uint32_t>(result_bits >> 32u);\n            if (instruction.dst_reg == instruction.src_reg) state.r[instruction.dst_reg] = dst_address + 8u;\n            else {\n                state.r[instruction.dst_reg] = dst_address + 4u;\n                state.r[instruction.src_reg] = src_address + 4u;\n            }\n            return Result<void>::success();\n        }\n        case Sh4IrOp::multiply_accumulate_word: {\n            auto dst = require_register(instruction.dst_reg);\n            if (!dst) return dst;\n            auto src = require_register(instruction.src_reg);\n            if (!src) return src;\n            const auto dst_address = state.r[instruction.dst_reg];\n            const auto src_address = state.r[instruction.src_reg];\n            auto lhs_raw = read_u16(memory, src_address);\n            if (!lhs_raw) return Result<void>::failure(lhs_raw.error, lhs_raw.detail);\n            auto rhs_raw = read_u16(memory, dst_address);\n            if (!rhs_raw) return Result<void>::failure(rhs_raw.error, rhs_raw.detail);\n            const auto lhs = std::bit_cast<std::int16_t>(lhs_raw.value);\n            const auto rhs = std::bit_cast<std::int16_t>(rhs_raw.value);\n            const auto product = static_cast<std::int32_t>(lhs) * static_cast<std::int32_t>(rhs);\n            if ((state.sr & kSrSBit) != 0u) {\n                const auto accumulator = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(state.macl));\n                const auto sum = accumulator + static_cast<std::int64_t>(product);\n                if (sum > std::numeric_limits<std::int32_t>::max()) { state.macl = 0x7FFFFFFFu; state.mach = 1u; }\n                else if (sum < std::numeric_limits<std::int32_t>::min()) { state.macl = 0x80000000u; state.mach = 1u; }\n                else state.macl = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(sum));\n            } else {\n                const std::uint64_t combined = (static_cast<std::uint64_t>(state.mach) << 32u) | state.macl;\n                const auto result = combined + static_cast<std::uint64_t>(static_cast<std::int64_t>(product));\n                state.macl = static_cast<std::uint32_t>(result);\n                state.mach = static_cast<std::uint32_t>(result >> 32u);\n            }\n            if (instruction.dst_reg == instruction.src_reg) state.r[instruction.dst_reg] = dst_address + 4u;\n            else {\n                state.r[instruction.dst_reg] = dst_address + 2u;\n                state.r[instruction.src_reg] = src_address + 2u;\n            }\n            return Result<void>::success();\n        }\n        case Sh4IrOp::multiply_unsigned_long: {\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::set_fr_zero:\n",
    "        case Sh4IrOp::test_and_set_byte: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            const auto address = state.r[instruction.dst_reg];\n            auto value = read_u8(memory, address);\n            if (!value) return Result<void>::failure(value.error, value.detail);\n            state.t = value.value == 0u;\n            return write_u8(memory, address, static_cast<std::uint8_t>(value.value | 0x80u));\n        }\n        case Sh4IrOp::trap_imm:\n            if (instruction.in_delay_slot) {\n                enter_general_exception(instruction, state, pending, 0x1A0u);\n                return Result<void>::success();\n            }\n            state.spc = instruction.source_address + 2u;\n            state.ssr = read_sh4_reference_sr(state);\n            state.sgr = state.r[15];\n            state.tra = static_cast<std::uint32_t>(instruction.imm & 0xFF) << 2u;\n            state.expevt = 0x160u;\n            write_sh4_reference_sr(state, state.ssr | kSrMdBit | kSrRbBit | kSrBlBit);\n            pending = PendingTransfer{state.vbr + 0x100u, std::nullopt, true};\n            return Result<void>::success();\n        case Sh4IrOp::movca_long: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            const auto address = state.r[instruction.dst_reg];\n            auto stored = write_u32(memory, address, state.r[0]);\n            if (!stored) return stored;\n            state.last_system_event = Sh4ReferenceSystemEvent::movca_l;\n            state.system_event_address = address;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::ocbi_event:\n        case Sh4IrOp::ocbp_event:\n        case Sh4IrOp::ocbwb_event:\n        case Sh4IrOp::pref_event: {\n            auto reg = require_register(instruction.dst_reg);\n            if (!reg) return reg;\n            state.system_event_address = state.r[instruction.dst_reg];\n            if (instruction.op == Sh4IrOp::ocbi_event) state.last_system_event = Sh4ReferenceSystemEvent::ocbi;\n            else if (instruction.op == Sh4IrOp::ocbp_event) state.last_system_event = Sh4ReferenceSystemEvent::ocbp;\n            else if (instruction.op == Sh4IrOp::ocbwb_event) state.last_system_event = Sh4ReferenceSystemEvent::ocbwb;\n            else state.last_system_event = Sh4ReferenceSystemEvent::pref;\n            return Result<void>::success();\n        }\n        case Sh4IrOp::set_fr_zero:\n",
)
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        case Sh4IrOp::return_exception:\n            state.sr = state.ssr;\n            pending = PendingTransfer{state.spc, std::nullopt};\n            return Result<void>::success();\n",
    "        case Sh4IrOp::return_exception:\n            if (!require_privileged(instruction, state, pending)) return Result<void>::success();\n            write_sh4_reference_sr(state, state.ssr);\n            pending = PendingTransfer{state.spc, std::nullopt};\n            return Result<void>::success();\n",
)

# Immediate exceptions/traps and SLEEP terminate the current reference block.
replace_once(
    "src/core/sh4_reference_executor.cpp",
    "        for (const auto& instruction : block->ops) {\n            auto executed = execute_op(instruction, state, memory, pending);\n            if (!executed) return Result<Sh4ReferenceRunResult>::failure(executed.error, executed.detail);\n            ++run.operations_executed;\n        }\n        ++run.blocks_executed;\n",
    "        for (const auto& instruction : block->ops) {\n            auto executed = execute_op(instruction, state, memory, pending);\n            if (!executed) return Result<Sh4ReferenceRunResult>::failure(executed.error, executed.detail);\n            ++run.operations_executed;\n            if (state.sleeping) {\n                ++run.blocks_executed;\n                state.pc = instruction.source_address + 2u;\n                run.stop_reason = Sh4ReferenceStopReason::sleep;\n                return Result<Sh4ReferenceRunResult>::success(run);\n            }\n            if (pending && pending->immediate) break;\n        }\n        ++run.blocks_executed;\n        if (pending && pending->immediate) {\n            state.pc = pending->target;\n            if (!find_sh4_ir_block(program, state.pc)) {\n                run.stop_reason = Sh4ReferenceStopReason::left_program;\n                return Result<Sh4ReferenceRunResult>::success(run);\n            }\n            continue;\n        }\n",
)
