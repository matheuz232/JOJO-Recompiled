#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

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
    case 4u:
    case 12u:
    case 20u:
    case 26u:
    case 27u:
    case 29u:
    case 30u:
        gte.control[index] = psx_gte_sign_extend_u16(value);
        return;
    case 31u: {
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
    case 1u:
    case 3u:
    case 5u:
    case 8u:
    case 9u:
    case 10u:
    case 11u:
        gte.data[index] = psx_gte_sign_extend_u16(value);
        return;
    case 7u:
    case 16u:
    case 17u:
    case 18u:
    case 19u:
        gte.data[index] = value & 0xffffu;
        return;
    case 15u:
        gte.data[12] = gte.data[13];
        gte.data[13] = gte.data[14];
        gte.data[14] = value;
        return;
    case 28u: {
        gte.data[28] = value & 0x7fffu;
        const auto r = (value & 0x1fu) * 0x80u;
        const auto g = ((value >> 5u) & 0x1fu) * 0x80u;
        const auto b = ((value >> 10u) & 0x1fu) * 0x80u;
        gte.data[9] = psx_gte_sign_extend_u16(r);
        gte.data[10] = psx_gte_sign_extend_u16(g);
        gte.data[11] = psx_gte_sign_extend_u16(b);
        return;
    }
    case 29u:
        return;
    case 30u:
        gte.data[30] = value;
        gte.data[31] = psx_gte_count_leading_bits(value);
        return;
    case 31u:
        return;
    default:
        gte.data[index] = value;
        return;
    }
}

[[nodiscard]] inline std::uint32_t psx_gte_read_data(const PsxGteState& gte,
                                                      std::uint8_t index) noexcept {
    if (index == 15u) return gte.data[14];
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

[[nodiscard]] inline std::int32_t psx_gte_s16(std::uint32_t value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::int16_t>(value & 0xffffu));
}

[[nodiscard]] inline std::int64_t psx_gte_sar(std::int64_t value,
                                               std::uint32_t shift) noexcept {
    if (shift == 0u) return value;
    const auto divisor = std::int64_t{1} << shift;
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

[[nodiscard]] inline std::int64_t psx_gte_sar12(std::int64_t value) noexcept {
    return psx_gte_sar(value, 12u);
}

inline void psx_gte_finalize_flags(PsxGteState& gte) noexcept {
    constexpr std::uint32_t error_summary_mask = 0x7f87e000u;
    gte.control[31] &= 0x7ffff000u;
    if ((gte.control[31] & error_summary_mask) != 0u) {
        gte.control[31] |= 0x80000000u;
    }
}

inline void psx_gte_store_mac0(PsxGteState& gte, std::int64_t value) noexcept {
    constexpr std::uint32_t mac0_positive_overflow = 1u << 16u;
    constexpr std::uint32_t mac0_negative_overflow = 1u << 15u;
    if (value > std::numeric_limits<std::int32_t>::max()) {
        gte.control[31] |= mac0_positive_overflow;
    } else if (value < std::numeric_limits<std::int32_t>::min()) {
        gte.control[31] |= mac0_negative_overflow;
    }
    gte.data[24] = static_cast<std::uint32_t>(value);
}

inline void psx_gte_store_otz(PsxGteState& gte, std::int64_t value) noexcept {
    constexpr std::uint32_t sz_otz_saturation = 1u << 18u;
    if (value < 0) {
        gte.data[7] = 0u;
        gte.control[31] |= sz_otz_saturation;
    } else if (value > 0xffff) {
        gte.data[7] = 0xffffu;
        gte.control[31] |= sz_otz_saturation;
    } else {
        gte.data[7] = static_cast<std::uint32_t>(value);
    }
}

using PsxGteVector = std::array<std::int32_t, 3>;
using PsxGteMatrix = std::array<PsxGteVector, 3>;

[[nodiscard]] inline PsxGteVector psx_gte_vector(const PsxGteState& gte,
                                                  std::uint8_t selector) noexcept {
    switch (selector & 3u) {
    case 0u:
        return {psx_gte_s16(gte.data[0]), psx_gte_s16(gte.data[0] >> 16u),
                psx_gte_s16(gte.data[1])};
    case 1u:
        return {psx_gte_s16(gte.data[2]), psx_gte_s16(gte.data[2] >> 16u),
                psx_gte_s16(gte.data[3])};
    case 2u:
        return {psx_gte_s16(gte.data[4]), psx_gte_s16(gte.data[4] >> 16u),
                psx_gte_s16(gte.data[5])};
    default:
        return {psx_gte_s16(gte.data[9]), psx_gte_s16(gte.data[10]),
                psx_gte_s16(gte.data[11])};
    }
}

[[nodiscard]] inline PsxGteMatrix psx_gte_matrix_from_control(
    const PsxGteState& gte, std::uint8_t base) noexcept {
    return {{
        {psx_gte_s16(gte.control[base + 0u]),
         psx_gte_s16(gte.control[base + 0u] >> 16u),
         psx_gte_s16(gte.control[base + 1u])},
        {psx_gte_s16(gte.control[base + 1u] >> 16u),
         psx_gte_s16(gte.control[base + 2u]),
         psx_gte_s16(gte.control[base + 2u] >> 16u)},
        {psx_gte_s16(gte.control[base + 3u]),
         psx_gte_s16(gte.control[base + 3u] >> 16u),
         psx_gte_s16(gte.control[base + 4u])}
    }};
}

[[nodiscard]] inline PsxGteMatrix psx_gte_matrix(const PsxGteState& gte,
                                                   std::uint8_t selector) noexcept {
    switch (selector & 3u) {
    case 0u:
        return psx_gte_matrix_from_control(gte, 0u);
    case 1u:
        return psx_gte_matrix_from_control(gte, 8u);
    case 2u:
        return psx_gte_matrix_from_control(gte, 16u);
    default: {
        const auto r = static_cast<std::int32_t>(gte.data[6] & 0xffu);
        const auto ir0 = psx_gte_s16(gte.data[8]);
        const auto rt13 = psx_gte_s16(gte.control[1]);
        const auto rt22 = psx_gte_s16(gte.control[2]);
        return {{{-r * 0x10, r * 0x10, ir0},
                 {rt13, rt13, rt13},
                 {rt22, rt22, rt22}}};
    }
    }
}

[[nodiscard]] inline PsxGteVector psx_gte_translation(const PsxGteState& gte,
                                                       std::uint8_t selector) noexcept {
    const auto signed32 = [](std::uint32_t value) noexcept {
        return static_cast<std::int32_t>(value);
    };
    switch (selector & 3u) {
    case 0u:
        return {signed32(gte.control[5]), signed32(gte.control[6]),
                signed32(gte.control[7])};
    case 1u:
        return {signed32(gte.control[13]), signed32(gte.control[14]),
                signed32(gte.control[15])};
    case 2u:
        return {signed32(gte.control[21]), signed32(gte.control[22]),
                signed32(gte.control[23])};
    default:
        return {0, 0, 0};
    }
}

inline void psx_gte_check_mac123_overflow(PsxGteState& gte,
                                           std::uint8_t component,
                                           std::int64_t value) noexcept {
    constexpr std::int64_t max_mac44 = (std::int64_t{1} << 43u) - 1;
    constexpr std::int64_t min_mac44 = -(std::int64_t{1} << 43u);
    const auto positive_bit = static_cast<std::uint32_t>(30u - component);
    const auto negative_bit = static_cast<std::uint32_t>(27u - component);
    if (value > max_mac44) {
        gte.control[31] |= std::uint32_t{1} << positive_bit;
    } else if (value < min_mac44) {
        gte.control[31] |= std::uint32_t{1} << negative_bit;
    }
}

inline void psx_gte_store_mac_ir(PsxGteState& gte,
                                 std::uint8_t component,
                                 std::int64_t raw_value,
                                 bool shift_fraction,
                                 bool limit_mode) noexcept {
    psx_gte_check_mac123_overflow(gte, component, raw_value);
    const auto value = shift_fraction ? psx_gte_sar12(raw_value) : raw_value;
    gte.data[25u + component] = static_cast<std::uint32_t>(value);

    const auto minimum = limit_mode ? std::int64_t{0} : std::int64_t{-0x8000};
    constexpr std::int64_t maximum = 0x7fff;
    auto saturated = value;
    if (saturated < minimum) {
        saturated = minimum;
        gte.control[31] |= std::uint32_t{1} << (24u - component);
    } else if (saturated > maximum) {
        saturated = maximum;
        gte.control[31] |= std::uint32_t{1} << (24u - component);
    }
    gte.data[9u + component] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(saturated));
}

inline void psx_gte_push_color_fifo(PsxGteState& gte) noexcept {
    std::uint32_t packed = gte.data[6] & 0xff000000u;
    for (std::uint8_t component = 0u; component < 3u; ++component) {
        const auto mac = static_cast<std::int32_t>(gte.data[25u + component]);
        const auto divided = static_cast<std::int64_t>(mac) / 16ll;
        auto color = divided;
        if (color < 0) {
            color = 0;
            gte.control[31] |= std::uint32_t{1} << (21u - component);
        } else if (color > 0xff) {
            color = 0xff;
            gte.control[31] |= std::uint32_t{1} << (21u - component);
        }
        packed |= static_cast<std::uint32_t>(color) << (component * 8u);
    }
    gte.data[20] = gte.data[21];
    gte.data[21] = gte.data[22];
    gte.data[22] = packed;
}

inline void psx_gte_execute_mvmva(PsxGteState& gte,
                                  std::uint32_t instruction) noexcept {
    const bool sf = (instruction & (1u << 19u)) != 0u;
    const auto mx = static_cast<std::uint8_t>((instruction >> 17u) & 3u);
    const auto v = static_cast<std::uint8_t>((instruction >> 15u) & 3u);
    const auto cv = static_cast<std::uint8_t>((instruction >> 13u) & 3u);
    const bool lm = (instruction & (1u << 10u)) != 0u;

    const auto matrix = psx_gte_matrix(gte, mx);
    const auto vector = psx_gte_vector(gte, v);
    const auto translation = psx_gte_translation(gte, cv);

    for (std::uint8_t row = 0u; row < 3u; ++row) {
        const auto full_value =
            static_cast<std::int64_t>(translation[row]) * 0x1000ll +
            static_cast<std::int64_t>(matrix[row][0]) * vector[0] +
            static_cast<std::int64_t>(matrix[row][1]) * vector[1] +
            static_cast<std::int64_t>(matrix[row][2]) * vector[2];
        const auto result_value = cv == 2u
            ? static_cast<std::int64_t>(matrix[row][1]) * vector[1] +
              static_cast<std::int64_t>(matrix[row][2]) * vector[2]
            : full_value;

        psx_gte_check_mac123_overflow(gte, row, full_value);
        const auto shifted = sf ? psx_gte_sar12(result_value) : result_value;
        gte.data[25u + row] = static_cast<std::uint32_t>(shifted);

        const auto minimum = lm ? std::int64_t{0} : std::int64_t{-0x8000};
        constexpr std::int64_t maximum = 0x7fff;
        auto saturated = shifted;
        if (saturated < minimum) {
            saturated = minimum;
            gte.control[31] |= std::uint32_t{1} << (24u - row);
        } else if (saturated > maximum) {
            saturated = maximum;
            gte.control[31] |= std::uint32_t{1} << (24u - row);
        }
        gte.data[9u + row] = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(saturated));
    }
}

inline void psx_gte_execute_sqr(PsxGteState& gte,
                                std::uint32_t instruction) noexcept {
    const bool sf = (instruction & (1u << 19u)) != 0u;
    const auto ir = psx_gte_vector(gte, 3u);
    for (std::uint8_t component = 0u; component < 3u; ++component) {
        const auto raw = static_cast<std::int64_t>(ir[component]) * ir[component];
        psx_gte_store_mac_ir(gte, component, raw, sf, false);
    }
}

inline void psx_gte_execute_op(PsxGteState& gte,
                               std::uint32_t instruction) noexcept {
    const bool sf = (instruction & (1u << 19u)) != 0u;
    const bool lm = (instruction & (1u << 10u)) != 0u;
    const auto ir = psx_gte_vector(gte, 3u);
    const PsxGteVector diagonal{
        psx_gte_s16(gte.control[0]),
        psx_gte_s16(gte.control[2]),
        psx_gte_s16(gte.control[4]),
    };
    const std::array<std::int64_t, 3> raw{
        static_cast<std::int64_t>(ir[2]) * diagonal[1] -
            static_cast<std::int64_t>(ir[1]) * diagonal[2],
        static_cast<std::int64_t>(ir[0]) * diagonal[2] -
            static_cast<std::int64_t>(ir[2]) * diagonal[0],
        static_cast<std::int64_t>(ir[1]) * diagonal[0] -
            static_cast<std::int64_t>(ir[0]) * diagonal[1],
    };
    for (std::uint8_t component = 0u; component < 3u; ++component) {
        psx_gte_store_mac_ir(gte, component, raw[component], sf, lm);
    }
}

inline void psx_gte_execute_gpf_gpl(PsxGteState& gte,
                                    std::uint32_t instruction,
                                    bool accumulate) noexcept {
    const bool sf = (instruction & (1u << 19u)) != 0u;
    const bool lm = (instruction & (1u << 10u)) != 0u;
    const auto ir0 = static_cast<std::int64_t>(psx_gte_s16(gte.data[8]));
    const auto ir = psx_gte_vector(gte, 3u);
    const std::array<std::int32_t, 3> old_mac{
        static_cast<std::int32_t>(gte.data[25]),
        static_cast<std::int32_t>(gte.data[26]),
        static_cast<std::int32_t>(gte.data[27]),
    };

    for (std::uint8_t component = 0u; component < 3u; ++component) {
        const auto base = accumulate
            ? static_cast<std::int64_t>(old_mac[component]) * (sf ? 0x1000ll : 1ll)
            : 0ll;
        const auto raw = base + static_cast<std::int64_t>(ir[component]) * ir0;
        psx_gte_store_mac_ir(gte, component, raw, sf, lm);
    }
    psx_gte_push_color_fifo(gte);
}

[[nodiscard]] inline std::uint32_t psx_gte_unr_table_value(std::uint32_t index) noexcept {
    const auto denominator = index + 0x100u;
    const auto value = static_cast<std::int32_t>(
        ((0x40000u / denominator + 1u) / 2u)) - 0x101;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0u;
}

[[nodiscard]] inline std::uint32_t psx_gte_perspective_divide(
    PsxGteState& gte, std::uint32_t h, std::uint32_t sz3) noexcept {
    h &= 0xffffu;
    sz3 &= 0xffffu;
    if (static_cast<std::uint64_t>(h) >= static_cast<std::uint64_t>(sz3) * 2u) {
        gte.control[31] |= 1u << 17u;
        return 0x1ffffu;
    }

    std::uint32_t z = 0u;
    for (std::uint32_t mask = 0x8000u; mask != 0u && (sz3 & mask) == 0u; mask >>= 1u) {
        ++z;
    }
    const auto n = h << z;
    auto d = sz3 << z;
    const auto index = (d - 0x7fc0u) >> 7u;
    const auto u = psx_gte_unr_table_value(index) + 0x101u;
    d = (0x2000080u - d * u) >> 8u;
    d = (0x0000080u + d * u) >> 8u;
    const auto result = (static_cast<std::uint64_t>(n) * d + 0x8000u) >> 16u;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(0x1ffffu, result));
}

inline void psx_gte_push_sz(PsxGteState& gte, std::int64_t value) noexcept {
    constexpr std::uint32_t saturation_flag = 1u << 18u;
    auto saturated = value;
    if (saturated < 0) {
        saturated = 0;
        gte.control[31] |= saturation_flag;
    } else if (saturated > 0xffff) {
        saturated = 0xffff;
        gte.control[31] |= saturation_flag;
    }
    gte.data[16] = gte.data[17];
    gte.data[17] = gte.data[18];
    gte.data[18] = gte.data[19];
    gte.data[19] = static_cast<std::uint32_t>(saturated);
}

[[nodiscard]] inline std::int16_t psx_gte_saturate_screen(PsxGteState& gte,
                                                           std::int64_t value,
                                                           std::uint32_t flag_bit) noexcept {
    if (value < -0x400) {
        gte.control[31] |= 1u << flag_bit;
        return static_cast<std::int16_t>(-0x400);
    }
    if (value > 0x3ff) {
        gte.control[31] |= 1u << flag_bit;
        return static_cast<std::int16_t>(0x3ff);
    }
    return static_cast<std::int16_t>(value);
}

inline void psx_gte_push_sxy(PsxGteState& gte,
                             std::uint32_t divide_result) noexcept {
    const auto ir1 = static_cast<std::int32_t>(gte.data[9]);
    const auto ir2 = static_cast<std::int32_t>(gte.data[10]);
    const auto ofx = static_cast<std::int32_t>(gte.control[24]);
    const auto ofy = static_cast<std::int32_t>(gte.control[25]);

    const auto mac0x = static_cast<std::int64_t>(divide_result) * ir1 + ofx;
    psx_gte_store_mac0(gte, mac0x);
    const auto sx = psx_gte_saturate_screen(gte, psx_gte_sar(mac0x, 16u), 14u);

    const auto mac0y = static_cast<std::int64_t>(divide_result) * ir2 + ofy;
    psx_gte_store_mac0(gte, mac0y);
    const auto sy = psx_gte_saturate_screen(gte, psx_gte_sar(mac0y, 16u), 13u);

    gte.data[12] = gte.data[13];
    gte.data[13] = gte.data[14];
    gte.data[14] = static_cast<std::uint16_t>(sx) |
                   (static_cast<std::uint32_t>(static_cast<std::uint16_t>(sy)) << 16u);
}

inline void psx_gte_update_ir0(PsxGteState& gte,
                               std::uint32_t divide_result) noexcept {
    const auto dqa = psx_gte_s16(gte.control[27]);
    const auto dqb = static_cast<std::int32_t>(gte.control[28]);
    const auto mac0 = static_cast<std::int64_t>(divide_result) * dqa + dqb;
    psx_gte_store_mac0(gte, mac0);
    auto ir0 = psx_gte_sar12(mac0);
    if (ir0 < 0) {
        ir0 = 0;
        gte.control[31] |= 1u << 12u;
    } else if (ir0 > 0x1000) {
        ir0 = 0x1000;
        gte.control[31] |= 1u << 12u;
    }
    gte.data[8] = static_cast<std::uint32_t>(ir0);
}

inline void psx_gte_transform_project_vertex(PsxGteState& gte,
                                             std::uint8_t vector_selector,
                                             std::uint32_t instruction,
                                             bool update_ir0) noexcept {
    const bool sf = (instruction & (1u << 19u)) != 0u;
    const bool lm = (instruction & (1u << 10u)) != 0u;
    const auto matrix = psx_gte_matrix_from_control(gte, 0u);
    const auto vector = psx_gte_vector(gte, vector_selector);
    const PsxGteVector translation{
        static_cast<std::int32_t>(gte.control[5]),
        static_cast<std::int32_t>(gte.control[6]),
        static_cast<std::int32_t>(gte.control[7]),
    };

    std::array<std::int64_t, 3> mac{};
    for (std::uint8_t row = 0u; row < 3u; ++row) {
        const auto raw =
            static_cast<std::int64_t>(translation[row]) * 0x1000ll +
            static_cast<std::int64_t>(matrix[row][0]) * vector[0] +
            static_cast<std::int64_t>(matrix[row][1]) * vector[1] +
            static_cast<std::int64_t>(matrix[row][2]) * vector[2];
        psx_gte_check_mac123_overflow(gte, row, raw);
        mac[row] = sf ? psx_gte_sar12(raw) : raw;
        gte.data[25u + row] = static_cast<std::uint32_t>(mac[row]);

        const auto minimum = lm ? std::int64_t{0} : std::int64_t{-0x8000};
        constexpr std::int64_t maximum = 0x7fff;
        auto saturated = mac[row];
        if (saturated < minimum) {
            saturated = minimum;
            gte.control[31] |= 1u << (24u - row);
        } else if (saturated > maximum) {
            saturated = maximum;
            gte.control[31] |= 1u << (24u - row);
        }
        gte.data[9u + row] = static_cast<std::uint32_t>(static_cast<std::int32_t>(saturated));
    }

    const auto ir3_flag_value = sf ? mac[2] : psx_gte_sar12(mac[2]);
    if (ir3_flag_value < -0x8000 || ir3_flag_value > 0x7fff) {
        gte.control[31] |= 1u << 22u;
    }

    const auto screen_z = sf ? mac[2] : psx_gte_sar12(mac[2]);
    psx_gte_push_sz(gte, screen_z);
    const auto divide_result = psx_gte_perspective_divide(
        gte, gte.control[26] & 0xffffu, gte.data[19]);
    psx_gte_push_sxy(gte, divide_result);
    if (update_ir0) psx_gte_update_ir0(gte, divide_result);
}

inline void psx_gte_execute_rtps(PsxGteState& gte,
                                 std::uint32_t instruction) noexcept {
    psx_gte_transform_project_vertex(gte, 0u, instruction, true);
}

inline void psx_gte_execute_rtpt(PsxGteState& gte,
                                 std::uint32_t instruction) noexcept {
    psx_gte_transform_project_vertex(gte, 0u, instruction, false);
    psx_gte_transform_project_vertex(gte, 1u, instruction, false);
    psx_gte_transform_project_vertex(gte, 2u, instruction, true);
}

[[nodiscard]] inline bool execute_psx_gte_command(PsxGteState& gte,
                                                   std::uint32_t instruction) noexcept {
    gte.control[31] = 0u;
    const auto command = static_cast<std::uint8_t>(instruction & 0x3fu);

    switch (command) {
    case 0x01u:
        psx_gte_execute_rtps(gte, instruction);
        psx_gte_finalize_flags(gte);
        return true;
    case 0x06u: {
        const auto sx0 = psx_gte_s16(gte.data[12]);
        const auto sy0 = psx_gte_s16(gte.data[12] >> 16u);
        const auto sx1 = psx_gte_s16(gte.data[13]);
        const auto sy1 = psx_gte_s16(gte.data[13] >> 16u);
        const auto sx2 = psx_gte_s16(gte.data[14]);
        const auto sy2 = psx_gte_s16(gte.data[14] >> 16u);
        const std::int64_t mac0 =
            static_cast<std::int64_t>(sx0) * sy1 +
            static_cast<std::int64_t>(sx1) * sy2 +
            static_cast<std::int64_t>(sx2) * sy0 -
            static_cast<std::int64_t>(sx0) * sy2 -
            static_cast<std::int64_t>(sx1) * sy0 -
            static_cast<std::int64_t>(sx2) * sy1;
        psx_gte_store_mac0(gte, mac0);
        psx_gte_finalize_flags(gte);
        return true;
    }
    case 0x0cu:
        psx_gte_execute_op(gte, instruction);
        psx_gte_finalize_flags(gte);
        return true;
    case 0x12u:
        psx_gte_execute_mvmva(gte, instruction);
        psx_gte_finalize_flags(gte);
        return true;
    case 0x28u:
        psx_gte_execute_sqr(gte, instruction);
        psx_gte_finalize_flags(gte);
        return true;
    case 0x2du: {
        const auto zsf3 = static_cast<std::int32_t>(gte.control[29]);
        const auto sum = static_cast<std::int64_t>(gte.data[17] & 0xffffu) +
                         static_cast<std::int64_t>(gte.data[18] & 0xffffu) +
                         static_cast<std::int64_t>(gte.data[19] & 0xffffu);
        const auto mac0 = static_cast<std::int64_t>(zsf3) * sum;
        psx_gte_store_mac0(gte, mac0);
        psx_gte_store_otz(gte, psx_gte_sar12(mac0));
        psx_gte_finalize_flags(gte);
        return true;
    }
    case 0x2eu: {
        const auto zsf4 = static_cast<std::int32_t>(gte.control[30]);
        const auto sum = static_cast<std::int64_t>(gte.data[16] & 0xffffu) +
                         static_cast<std::int64_t>(gte.data[17] & 0xffffu) +
                         static_cast<std::int64_t>(gte.data[18] & 0xffffu) +
                         static_cast<std::int64_t>(gte.data[19] & 0xffffu);
        const auto mac0 = static_cast<std::int64_t>(zsf4) * sum;
        psx_gte_store_mac0(gte, mac0);
        psx_gte_store_otz(gte, psx_gte_sar12(mac0));
        psx_gte_finalize_flags(gte);
        return true;
    }
    case 0x30u:
        psx_gte_execute_rtpt(gte, instruction);
        psx_gte_finalize_flags(gte);
        return true;
    case 0x3du:
        psx_gte_execute_gpf_gpl(gte, instruction, false);
        psx_gte_finalize_flags(gte);
        return true;
    case 0x3eu:
        psx_gte_execute_gpf_gpl(gte, instruction, true);
        psx_gte_finalize_flags(gte);
        return true;
    default:
        return false;
    }
}

} // namespace jojo
