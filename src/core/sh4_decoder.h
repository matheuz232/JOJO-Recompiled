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
    mov_imm,
    add_imm,
    mov_reg,
    add_reg,
    sub_reg,
    cmp_eq_reg,
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
