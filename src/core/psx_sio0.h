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

namespace detail {

inline void clear_psx_sio0_transaction(PsxSio0State& sio) noexcept {
    sio.rx_read_index = 0u;
    sio.rx_count = 0u;
    sio.pad_phase = 0u;
}

[[nodiscard]] inline bool push_psx_sio0_rx(PsxSio0State& sio,
                                            std::uint8_t value) noexcept {
    if (sio.rx_count >= sio.rx_fifo.size()) return false;
    const auto index = static_cast<std::size_t>(
        (static_cast<unsigned>(sio.rx_read_index) +
         static_cast<unsigned>(sio.rx_count)) % sio.rx_fifo.size());
    sio.rx_fifo[index] = value;
    ++sio.rx_count;
    return true;
}

}

inline void reset_psx_sio0(PsxSio0State& sio) noexcept {
    const auto pads = sio.pads;
    sio = PsxSio0State{};
    sio.pads = pads;
}

inline void psx_sio0_set_pads(PsxSio0State& sio,
                              const PsxDigitalPadFrame& pads) noexcept {
    sio.pads = pads;
}

inline void psx_sio0_write_mode(PsxSio0State& sio, std::uint16_t value) noexcept {
    sio.mode = value;
}

inline void psx_sio0_write_control(PsxSio0State& sio,
                                   std::uint16_t value) noexcept {
    if ((value & psx_sio0_control_reset) != 0u) {
        const auto pads = sio.pads;
        const auto mode = sio.mode;
        const auto baud = sio.baud;
        sio = PsxSio0State{};
        sio.pads = pads;
        sio.mode = mode;
        sio.baud = baud;
        return;
    }

    const auto previous_selection = static_cast<std::uint16_t>(
        sio.control & (psx_sio0_control_dtr | psx_sio0_control_port_2));
    const auto next_selection = static_cast<std::uint16_t>(
        value & (psx_sio0_control_dtr | psx_sio0_control_port_2));
    sio.control = value;
    if (previous_selection != next_selection ||
        (value & psx_sio0_control_dtr) == 0u) {
        detail::clear_psx_sio0_transaction(sio);
    }
}

inline void psx_sio0_write_baud(PsxSio0State& sio, std::uint16_t value) noexcept {
    sio.baud = value;
}

[[nodiscard]] inline std::uint32_t psx_sio0_status(const PsxSio0State& sio) noexcept {
    auto status = psx_sio0_status_tx_ready_1 | psx_sio0_status_tx_ready_2;
    if (sio.rx_count != 0u) status |= psx_sio0_status_rx_not_empty;
    return status;
}

[[nodiscard]] inline bool psx_sio0_write_data(PsxSio0State& sio,
                                               std::uint8_t value) noexcept {
    if ((sio.control & psx_sio0_control_tx_enable) == 0u ||
        (sio.control & psx_sio0_control_dtr) == 0u) {
        return false;
    }

    const auto port = (sio.control & psx_sio0_control_port_2) != 0u ? 1u : 0u;
    const auto response = psx_digital_pad_poll_response(sio.pads[port]);
    std::uint8_t rx = 0xffu;

    switch (sio.pad_phase) {
    case 0u:
        if (value == 0x01u) {
            rx = response[0];
            sio.pad_phase = 1u;
        }
        break;
    case 1u:
        if (value == 0x42u) {
            rx = response[1];
            sio.pad_phase = 2u;
        } else {
            sio.pad_phase = 0u;
        }
        break;
    case 2u:
        rx = response[2];
        sio.pad_phase = 3u;
        break;
    case 3u:
        rx = response[3];
        sio.pad_phase = 4u;
        break;
    case 4u:
        rx = response[4];
        sio.pad_phase = 5u;
        break;
    default:
        rx = 0xffu;
        break;
    }

    return detail::push_psx_sio0_rx(sio, rx);
}

[[nodiscard]] inline std::uint8_t psx_sio0_read_data(PsxSio0State& sio) noexcept {
    if (sio.rx_count == 0u) return 0xffu;
    const auto value = sio.rx_fifo[static_cast<std::size_t>(sio.rx_read_index)];
    sio.rx_read_index = static_cast<std::uint8_t>(
        (static_cast<unsigned>(sio.rx_read_index) + 1u) % sio.rx_fifo.size());
    --sio.rx_count;
    if (sio.rx_count == 0u) sio.rx_read_index = 0u;
    return value;
}

}
