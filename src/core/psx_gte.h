#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace jojo {

struct PsxGteState {
    std::array<std::uint32_t, 32> data{};
    std::array<std::uint32_t, 32> control{};
};

[[nodiscard]] inline std::uint32_t psx_gte_sign_extend_u16(std::uint32_t value) noexcept {
    const auto low = static_cast<std::uint16_t>(value);
    return (low & 0x8000u) != 0u ? (0xffff0000u | low) : static_cast<std::uint32_t>(low);
}

[[nodiscard]] inline std::uint32_t psx_gte_count_leading_bits(std::uint32_t value) noexcept {
    if ((value & 0x80000000u) != 0u) value = ~value;
    if (value == 0u) return 32u;

    std::uint32_t count = 0u;
    for (std::uint32_t mask = 0x80000000u; (value & mask) == 0u; mask >>= 1u) {
        ++count;
    }
    return count;
}

inline void psx_gte_write_control(PsxGteState& gte,
                                  std::uint8_t index,
                                  std::uint32_t value) noexcept {
    switch (index) {
    case 4u:  // RT33
    case 12u: // L33
    case 20u: // LR33
    case 26u: // H (unsigned for calculations, sign-extended on transfer reads)
    case 27u: // DQA
    case 29u: // ZSF3
    case 30u: // ZSF4
        gte.control[index] = psx_gte_sign_extend_u16(value);
        return;
    case 31u: { // FLAG
        constexpr std::uint32_t writable_mask = 0x7ffff000u;
        constexpr std::uint32_t error_summary_mask = 0x7f87e000u;
        auto flags = value & writable_mask;
        if ((flags & error_summary_mask) != 0u) flags |= 0x80000000u;
        gte.control[index] = flags;
        return;
    }
    default:
        gte.control[index] = value;
        return;
    }
}

[[nodiscard]] inline std::uint32_t psx_gte_read_control(const PsxGteState& gte,
                                                         std::uint8_t index) noexcept {
    return gte.control[index];
}

inline void psx_gte_write_data(PsxGteState& gte,
                               std::uint8_t index,
                               std::uint32_t value) noexcept {
    switch (index) {
    case 1u:  // VZ0
    case 3u:  // VZ1
    case 5u:  // VZ2
    case 8u:  // IR0
    case 9u:  // IR1
    case 10u: // IR2
    case 11u: // IR3
        gte.data[index] = psx_gte_sign_extend_u16(value);
        return;
    case 7u:  // OTZ
    case 16u: // SZ0
    case 17u: // SZ1
    case 18u: // SZ2
    case 19u: // SZ3
        gte.data[index] = value & 0xffffu;
        return;
    case 15u: // SXYP: push SXY FIFO
        gte.data[12] = gte.data[13];
        gte.data[13] = gte.data[14];
        gte.data[14] = value;
        return;
    case 28u: { // IRGB: 5:5:5 input expands into IR1/IR2/IR3
        gte.data[28] = value & 0x7fffu;
        const auto r = (value & 0x1fu) * 0x80u;
        const auto g = ((value >> 5u) & 0x1fu) * 0x80u;
        const auto b = ((value >> 10u) & 0x1fu) * 0x80u;
        gte.data[9] = psx_gte_sign_extend_u16(r);
        gte.data[10] = psx_gte_sign_extend_u16(g);
        gte.data[11] = psx_gte_sign_extend_u16(b);
        return;
    }
    case 29u: // ORGB is read-only
        return;
    case 30u: // LZCS
        gte.data[30] = value;
        gte.data[31] = psx_gte_count_leading_bits(value);
        return;
    case 31u: // LZCR is read-only
        return;
    default:
        gte.data[index] = value;
        return;
    }
}

[[nodiscard]] inline std::uint32_t psx_gte_read_data(const PsxGteState& gte,
                                                      std::uint8_t index) noexcept {
    if (index == 15u) return gte.data[14]; // SXY3 mirrors SXY2
    if (index == 28u || index == 29u) {
        const auto component = [&](std::uint8_t ir_index) noexcept -> std::uint32_t {
            const auto signed_value = static_cast<std::int32_t>(gte.data[ir_index]);
            const auto scaled = std::clamp(signed_value / 0x80, 0, 0x1f);
            return static_cast<std::uint32_t>(scaled);
        };
        return component(9u) | (component(10u) << 5u) | (component(11u) << 10u);
    }
    return gte.data[index];
}

} // namespace jojo
