#pragma once

#include "core/psx_pad.h"
#include <array>
#include <cstdint>

namespace jojo {

struct PsxSio0State {
    std::uint16_t mode{};
    std::uint16_t control{};
    std::uint16_t baud{};
    PsxDigitalPadFrame pads{};
    std::array<std::uint8_t, 8> rx_fifo{};
    std::uint8_t rx_read_index{};
    std::uint8_t rx_count{};
    std::uint8_t pad_phase{};
};

inline constexpr std::uint16_t psx_sio0_control_tx_enable = 1u << 0u;
inline constexpr std::uint16_t psx_sio0_control_dtr = 1u << 1u;
inline constexpr std::uint16_t psx_sio0_control_reset = 1u << 6u;
inline constexpr std::uint16_t psx_sio0_control_port_2 = 1u << 13u;

inline constexpr std::uint32_t psx_sio0_status_tx_ready_1 = 1u << 0u;
inline constexpr std::uint32_t psx_sio0_status_rx_not_empty = 1u << 1u;
inline constexpr std::uint32_t psx_sio0_status_tx_ready_2 = 1u << 2u;

void reset_psx_sio0(PsxSio0State& sio) noexcept;
void psx_sio0_set_pads(PsxSio0State& sio, const PsxDigitalPadFrame& pads) noexcept;
void psx_sio0_write_mode(PsxSio0State& sio, std::uint16_t value) noexcept;
void psx_sio0_write_control(PsxSio0State& sio, std::uint16_t value) noexcept;
void psx_sio0_write_baud(PsxSio0State& sio, std::uint16_t value) noexcept;
[[nodiscard]] std::uint32_t psx_sio0_status(const PsxSio0State& sio) noexcept;
[[nodiscard]] bool psx_sio0_write_data(PsxSio0State& sio, std::uint8_t value) noexcept;
[[nodiscard]] std::uint8_t psx_sio0_read_data(PsxSio0State& sio) noexcept;

}
