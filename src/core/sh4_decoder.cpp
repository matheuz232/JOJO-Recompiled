#include "core/sh4_decoder.h"
#include <limits>

namespace jojo {
namespace {
constexpr std::int32_t sign_extend8(std::uint16_t value) noexcept {
    const auto v = static_cast<std::uint8_t>(value & 0xFFu);
    return (v & 0x80u) ? static_cast<std::int32_t>(static_cast<std::int16_t>(0xFF00u | v))
                       : static_cast<std::int32_t>(v);
}

constexpr std::int32_t sign_extend12(std::uint16_t value) noexcept {
    const auto v = static_cast<std::uint16_t>(value & 0x0FFFu);
    return (v & 0x0800u) ? static_cast<std::int32_t>(static_cast<std::int16_t>(0xF000u | v))
                         : static_cast<std::int32_t>(v);
}

constexpr std::uint8_t n_field(std::uint16_t raw) noexcept {
    return static_cast<std::uint8_t>((raw >> 8) & 0x0Fu);
}

constexpr std::uint8_t m_field(std::uint16_t raw) noexcept {
    return static_cast<std::uint8_t>((raw >> 4) & 0x0Fu);
}

Sh4Instruction simple(std::uint16_t raw, std::uint32_t address, Sh4Op op) noexcept {
    Sh4Instruction i{};
    i.op = op;
    i.raw = raw;
    i.address = address;
    return i;
}
}

Sh4Instruction decode_sh4(std::uint16_t raw, std::uint32_t address) noexcept {
    auto i = simple(raw, address, Sh4Op::unsupported);

    if (raw == 0x0009u) {
        i.op = Sh4Op::nop;
        return i;
    }
    if (raw == 0x000Bu) {
        i.op = Sh4Op::rts;
        i.is_branch = true;
        i.has_delay_slot = true;
        return i;
    }
    if (raw == 0x002Bu) {
        i.op = Sh4Op::rte;
        i.is_branch = true;
        i.has_delay_slot = true;
        return i;
    }
    if (raw == 0x0008u) {
        i.op = Sh4Op::clrt;
        return i;
    }
    if (raw == 0x0018u) {
        i.op = Sh4Op::sett;
        return i;
    }
    if ((raw & 0xF0FFu) == 0x0029u) {
        i.op = Sh4Op::movt;
        i.rn = n_field(raw);
        return i;
    }

    if ((raw & 0xF000u) == 0xE000u) {
        i.op = Sh4Op::mov_imm;
        i.rn = n_field(raw);
        i.immediate = sign_extend8(raw);
        return i;
    }
    if ((raw & 0xF000u) == 0x7000u) {
        i.op = Sh4Op::add_imm;
        i.rn = n_field(raw);
        i.immediate = sign_extend8(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x6003u) {
        i.op = Sh4Op::mov_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }

    const auto low_nibble = static_cast<std::uint16_t>(raw & 0x000Fu);
    if ((raw & 0xF000u) == 0x2000u && low_nibble <= 0x6u) {
        if (low_nibble == 0x0u) i.op = Sh4Op::movb_store;
        else if (low_nibble == 0x1u) i.op = Sh4Op::movw_store;
        else if (low_nibble == 0x2u) i.op = Sh4Op::movl_store;
        else if (low_nibble == 0x4u) i.op = Sh4Op::movb_store_predec;
        else if (low_nibble == 0x5u) i.op = Sh4Op::movw_store_predec;
        else if (low_nibble == 0x6u) i.op = Sh4Op::movl_store_predec;
        if (i.op != Sh4Op::unsupported) {
            i.rn = n_field(raw);
            i.rm = m_field(raw);
            return i;
        }
    }
    if ((raw & 0xF000u) == 0x6000u && low_nibble <= 0x6u) {
        if (low_nibble == 0x0u) i.op = Sh4Op::movb_load;
        else if (low_nibble == 0x1u) i.op = Sh4Op::movw_load;
        else if (low_nibble == 0x2u) i.op = Sh4Op::movl_load;
        else if (low_nibble == 0x4u) i.op = Sh4Op::movb_load_postinc;
        else if (low_nibble == 0x5u) i.op = Sh4Op::movw_load_postinc;
        else if (low_nibble == 0x6u) i.op = Sh4Op::movl_load_postinc;
        if (i.op != Sh4Op::unsupported) {
            i.rn = n_field(raw);
            i.rm = m_field(raw);
            return i;
        }
    }

    const auto high_byte = static_cast<std::uint16_t>(raw & 0xFF00u);
    if (high_byte == 0x8000u || high_byte == 0x8100u ||
        high_byte == 0x8400u || high_byte == 0x8500u) {
        const auto reg = static_cast<std::uint8_t>((raw >> 4) & 0x0Fu);
        const auto disp = static_cast<std::int32_t>(raw & 0x000Fu);
        if (high_byte == 0x8000u) {
            i.op = Sh4Op::movb_store_disp; i.rn = reg; i.rm = 0; i.displacement = disp;
        } else if (high_byte == 0x8100u) {
            i.op = Sh4Op::movw_store_disp; i.rn = reg; i.rm = 0; i.displacement = disp * 2;
        } else if (high_byte == 0x8400u) {
            i.op = Sh4Op::movb_load_disp; i.rn = 0; i.rm = reg; i.displacement = disp;
        } else {
            i.op = Sh4Op::movw_load_disp; i.rn = 0; i.rm = reg; i.displacement = disp * 2;
        }
        return i;
    }
    if ((raw & 0xF000u) == 0x1000u) {
        i.op = Sh4Op::movl_store_disp;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        i.displacement = static_cast<std::int32_t>(raw & 0x000Fu) * 4;
        return i;
    }
    if ((raw & 0xF000u) == 0x5000u) {
        i.op = Sh4Op::movl_load_disp;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        i.displacement = static_cast<std::int32_t>(raw & 0x000Fu) * 4;
        return i;
    }

    const auto indexed_code = static_cast<std::uint16_t>(raw & 0xF00Fu);
    if (indexed_code == 0x0004u || indexed_code == 0x0005u || indexed_code == 0x0006u ||
        indexed_code == 0x000Cu || indexed_code == 0x000Du || indexed_code == 0x000Eu) {
        if (indexed_code == 0x0004u) i.op = Sh4Op::movb_store_indexed;
        if (indexed_code == 0x0005u) i.op = Sh4Op::movw_store_indexed;
        if (indexed_code == 0x0006u) i.op = Sh4Op::movl_store_indexed;
        if (indexed_code == 0x000Cu) i.op = Sh4Op::movb_load_indexed;
        if (indexed_code == 0x000Du) i.op = Sh4Op::movw_load_indexed;
        if (indexed_code == 0x000Eu) i.op = Sh4Op::movl_load_indexed;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }

    if (high_byte == 0xC000u || high_byte == 0xC100u || high_byte == 0xC200u ||
        high_byte == 0xC400u || high_byte == 0xC500u || high_byte == 0xC600u) {
        const auto disp = static_cast<std::int32_t>(raw & 0x00FFu);
        if (high_byte == 0xC000u) {
            i.op = Sh4Op::movb_store_gbr_disp; i.displacement = disp;
        } else if (high_byte == 0xC100u) {
            i.op = Sh4Op::movw_store_gbr_disp; i.displacement = disp * 2;
        } else if (high_byte == 0xC200u) {
            i.op = Sh4Op::movl_store_gbr_disp; i.displacement = disp * 4;
        } else if (high_byte == 0xC400u) {
            i.op = Sh4Op::movb_load_gbr_disp; i.displacement = disp;
        } else if (high_byte == 0xC500u) {
            i.op = Sh4Op::movw_load_gbr_disp; i.displacement = disp * 2;
        } else {
            i.op = Sh4Op::movl_load_gbr_disp; i.displacement = disp * 4;
        }
        i.rn = 0;
        i.rm = 0;
        return i;
    }

    const auto control_code = static_cast<std::uint16_t>(raw & 0xF0FFu);
    if (control_code == 0x401Eu) {
        i.op = Sh4Op::ldc_gbr_reg;
        i.rm = n_field(raw);
        return i;
    }
    if (control_code == 0x0012u) {
        i.op = Sh4Op::stc_gbr_reg;
        i.rn = n_field(raw);
        return i;
    }
    if (control_code == 0x4017u) {
        i.op = Sh4Op::ldc_gbr_postinc;
        i.rm = n_field(raw);
        return i;
    }
    if (control_code == 0x4013u) {
        i.op = Sh4Op::stc_gbr_predec;
        i.rn = n_field(raw);
        return i;
    }

    if ((raw & 0xF00Fu) == 0x300Cu) {
        i.op = Sh4Op::add_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x3008u) {
        i.op = Sh4Op::sub_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x3000u) {
        i.op = Sh4Op::cmp_eq_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xFF00u) == 0x8800u) {
        i.op = Sh4Op::cmp_eq_imm;
        i.rn = 0;
        i.immediate = sign_extend8(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x3002u) {
        i.op = Sh4Op::cmp_hs_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x3003u) {
        i.op = Sh4Op::cmp_ge_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x3006u) {
        i.op = Sh4Op::cmp_hi_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x3007u) {
        i.op = Sh4Op::cmp_gt_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF0FFu) == 0x4011u) {
        i.op = Sh4Op::cmp_pz;
        i.rn = n_field(raw);
        return i;
    }
    if ((raw & 0xF0FFu) == 0x4015u) {
        i.op = Sh4Op::cmp_pl;
        i.rn = n_field(raw);
        return i;
    }

    if ((raw & 0xF00Fu) == 0x2008u ||
        (raw & 0xF00Fu) == 0x2009u ||
        (raw & 0xF00Fu) == 0x200Au ||
        (raw & 0xF00Fu) == 0x200Bu) {
        const auto low = static_cast<std::uint16_t>(raw & 0x000Fu);
        if (low == 0x8u) i.op = Sh4Op::tst_reg;
        if (low == 0x9u) i.op = Sh4Op::and_reg;
        if (low == 0xAu) i.op = Sh4Op::xor_reg;
        if (low == 0xBu) i.op = Sh4Op::or_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xFF00u) >= 0xC800u && (raw & 0xFF00u) <= 0xCB00u) {
        const auto high = static_cast<std::uint16_t>(raw & 0xFF00u);
        if (high == 0xC800u) i.op = Sh4Op::tst_imm;
        if (high == 0xC900u) i.op = Sh4Op::and_imm;
        if (high == 0xCA00u) i.op = Sh4Op::xor_imm;
        if (high == 0xCB00u) i.op = Sh4Op::or_imm;
        i.rn = 0;
        i.immediate = static_cast<std::int32_t>(raw & 0x00FFu);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x6007u) {
        i.op = Sh4Op::not_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }
    if ((raw & 0xF00Fu) == 0x600Bu) {
        i.op = Sh4Op::neg_reg;
        i.rn = n_field(raw);
        i.rm = m_field(raw);
        return i;
    }

    const auto shift_code = static_cast<std::uint16_t>(raw & 0xF0FFu);
    if (shift_code == 0x4000u || shift_code == 0x4001u || shift_code == 0x4021u ||
        shift_code == 0x4008u || shift_code == 0x4009u || shift_code == 0x4018u ||
        shift_code == 0x4019u || shift_code == 0x4028u || shift_code == 0x4029u) {
        if (shift_code == 0x4000u) i.op = Sh4Op::shll;
        if (shift_code == 0x4001u) i.op = Sh4Op::shlr;
        if (shift_code == 0x4021u) i.op = Sh4Op::shar;
        if (shift_code == 0x4008u) i.op = Sh4Op::shll2;
        if (shift_code == 0x4009u) i.op = Sh4Op::shlr2;
        if (shift_code == 0x4018u) i.op = Sh4Op::shll8;
        if (shift_code == 0x4019u) i.op = Sh4Op::shlr8;
        if (shift_code == 0x4028u) i.op = Sh4Op::shll16;
        if (shift_code == 0x4029u) i.op = Sh4Op::shlr16;
        i.rn = n_field(raw);
        return i;
    }

    if ((raw & 0xF000u) == 0xA000u) {
        i.op = Sh4Op::bra;
        i.displacement = sign_extend12(raw) * 2;
        i.is_branch = true;
        i.has_delay_slot = true;
        return i;
    }
    if ((raw & 0xF000u) == 0xB000u) {
        i.op = Sh4Op::bsr;
        i.displacement = sign_extend12(raw) * 2;
        i.is_branch = true;
        i.has_delay_slot = true;
        i.writes_pr = true;
        return i;
    }

    const auto high = static_cast<std::uint16_t>(raw & 0xFF00u);
    if (high == 0x8900u || high == 0x8B00u || high == 0x8D00u || high == 0x8F00u) {
        if (high == 0x8900u) i.op = Sh4Op::bt;
        if (high == 0x8B00u) i.op = Sh4Op::bf;
        if (high == 0x8D00u) i.op = Sh4Op::bt_s;
        if (high == 0x8F00u) i.op = Sh4Op::bf_s;
        i.displacement = sign_extend8(raw) * 2;
        i.is_branch = true;
        i.conditional = true;
        i.has_delay_slot = high == 0x8D00u || high == 0x8F00u;
        return i;
    }

    if ((raw & 0xF0FFu) == 0x402Bu) {
        i.op = Sh4Op::jmp_reg;
        i.rn = n_field(raw);
        i.is_branch = true;
        i.has_delay_slot = true;
        return i;
    }
    if ((raw & 0xF0FFu) == 0x400Bu) {
        i.op = Sh4Op::jsr_reg;
        i.rn = n_field(raw);
        i.is_branch = true;
        i.has_delay_slot = true;
        i.writes_pr = true;
        return i;
    }

    if ((raw & 0xF000u) == 0x9000u) {
        i.op = Sh4Op::movw_pc;
        i.rn = n_field(raw);
        i.displacement = static_cast<std::int32_t>(raw & 0x00FFu) * 2;
        return i;
    }
    if ((raw & 0xF000u) == 0xD000u) {
        i.op = Sh4Op::movl_pc;
        i.rn = n_field(raw);
        i.displacement = static_cast<std::int32_t>(raw & 0x00FFu) * 4;
        return i;
    }
    if ((raw & 0xFF00u) == 0xC700u) {
        i.op = Sh4Op::mova_pc;
        i.rn = 0;
        i.displacement = static_cast<std::int32_t>(raw & 0x00FFu) * 4;
        return i;
    }

    return i;
}

Result<std::vector<Sh4Instruction>> decode_sh4_stream(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t base_address) {
    if ((bytes.size() & 1u) != 0u) {
        return Result<std::vector<Sh4Instruction>>::failure(
            ErrorCode::invalid_argument, "SH-4 instruction stream has an odd byte count");
    }
    const auto instruction_count = bytes.size() / 2u;
    if (instruction_count >
        (static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - base_address) / 2u + 1u) {
        return Result<std::vector<Sh4Instruction>>::failure(
            ErrorCode::invalid_argument, "SH-4 instruction stream address range overflows 32-bit address space");
    }

    std::vector<Sh4Instruction> result;
    result.reserve(instruction_count);
    for (std::size_t index = 0; index < instruction_count; ++index) {
        const auto byte_index = index * 2u;
        const auto raw = static_cast<std::uint16_t>(bytes[byte_index]) |
                         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[byte_index + 1u]) << 8u);
        const auto address = base_address + static_cast<std::uint32_t>(index * 2u);
        result.push_back(decode_sh4(raw, address));
    }
    return Result<std::vector<Sh4Instruction>>::success(std::move(result));
}

std::optional<std::uint32_t> sh4_direct_target(const Sh4Instruction& instruction) noexcept {
    switch (instruction.op) {
        case Sh4Op::bra:
        case Sh4Op::bsr:
        case Sh4Op::bt:
        case Sh4Op::bf:
        case Sh4Op::bt_s:
        case Sh4Op::bf_s:
            return static_cast<std::uint32_t>(instruction.address + 4u + instruction.displacement);
        default:
            return std::nullopt;
    }
}

std::optional<std::uint32_t> sh4_pc_relative_address(const Sh4Instruction& instruction) noexcept {
    if (instruction.op == Sh4Op::movw_pc) {
        return static_cast<std::uint32_t>(instruction.address + 4u + instruction.displacement);
    }
    if (instruction.op == Sh4Op::movl_pc || instruction.op == Sh4Op::mova_pc) {
        const auto aligned_pc = static_cast<std::uint32_t>((instruction.address + 4u) & ~3u);
        return static_cast<std::uint32_t>(aligned_pc + instruction.displacement);
    }
    return std::nullopt;
}

}
