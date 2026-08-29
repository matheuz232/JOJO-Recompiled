#pragma once
#include "core/result.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

enum class Sh4Op {
    unsupported,
    nop,
    rts,
    rte,
    clrt,
    sett,
    movt,
    mov_imm,
    add_imm,
    mov_reg,
    movb_store,
    movw_store,
    movl_store,
    movb_load,
    movw_load,
    movl_load,
    movb_store_predec,
    movw_store_predec,
    movl_store_predec,
    movb_load_postinc,
    movw_load_postinc,
    movl_load_postinc,
    movb_store_disp,
    movw_store_disp,
    movl_store_disp,
    movb_load_disp,
    movw_load_disp,
    movl_load_disp,
    movb_store_indexed,
    movw_store_indexed,
    movl_store_indexed,
    movb_load_indexed,
    movw_load_indexed,
    movl_load_indexed,
    movb_store_gbr_disp,
    movw_store_gbr_disp,
    movl_store_gbr_disp,
    movb_load_gbr_disp,
    movw_load_gbr_disp,
    movl_load_gbr_disp,
    ldc_gbr_reg,
    stc_gbr_reg,
    ldc_gbr_postinc,
    stc_gbr_predec,
    lds_mach_reg,
    lds_macl_reg,
    lds_pr_reg,
    sts_mach_reg,
    sts_macl_reg,
    sts_pr_reg,
    lds_mach_postinc,
    lds_macl_postinc,
    lds_pr_postinc,
    sts_mach_predec,
    sts_macl_predec,
    sts_pr_predec,
    lds_fpul_reg,
    sts_fpul_reg,
    lds_fpul_postinc,
    sts_fpul_predec,
    clrmac,
    mul_l,
    muls_w,
    mulu_w,
    dmuls_l,
    dmulu_l,
    exts_b,
    exts_w,
    extu_b,
    extu_w,
    swap_b,
    swap_w,
    xtrct,
    add_reg,
    addc_reg,
    addv_reg,
    sub_reg,
    subc_reg,
    subv_reg,
    negc_reg,
    cmp_eq_reg,
    cmp_eq_imm,
    cmp_hs_reg,
    cmp_ge_reg,
    cmp_hi_reg,
    cmp_gt_reg,
    cmp_pz,
    cmp_pl,
    tst_reg,
    and_reg,
    xor_reg,
    or_reg,
    tst_imm,
    and_imm,
    xor_imm,
    or_imm,
    not_reg,
    neg_reg,
    shll,
    shlr,
    shar,
    shll2,
    shlr2,
    shll8,
    shlr8,
    shll16,
    shlr16,
    fldi0,
    fldi1,
    bra,
    bsr,
    bt,
    bf,
    bt_s,
    bf_s,
    jmp_reg,
    jsr_reg,
    movw_pc,
    movl_pc,
    mova_pc,
};

struct Sh4Instruction {
    Sh4Op op{Sh4Op::unsupported};
    std::uint16_t raw{};
    std::uint32_t address{};
    std::uint8_t rn{0xFF};
    std::uint8_t rm{0xFF};
    std::int32_t immediate{};
    std::int32_t displacement{};
    bool is_branch{};
    bool conditional{};
    bool has_delay_slot{};
    bool writes_pr{};
};

[[nodiscard]] Sh4Instruction decode_sh4(std::uint16_t raw,
                                        std::uint32_t address) noexcept;

[[nodiscard]] Result<std::vector<Sh4Instruction>> decode_sh4_stream(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t base_address);

[[nodiscard]] std::optional<std::uint32_t> sh4_direct_target(
    const Sh4Instruction& instruction) noexcept;

[[nodiscard]] std::optional<std::uint32_t> sh4_pc_relative_address(
    const Sh4Instruction& instruction) noexcept;

}
