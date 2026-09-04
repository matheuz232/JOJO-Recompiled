#pragma once

#include "core/dreamcast_bus.h"
#include "core/dreamcast_memory.h"
#include "core/dreamcast_system_asic.h"
#include "core/input.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

class DreamcastMaple final : public DreamcastMmioDevice {
public:
    DreamcastMaple(DreamcastExecutableMemory& memory,
                   DreamcastSystemAsic& system_asic) noexcept
        : memory_(&memory), system_asic_(&system_asic) {}

    DreamcastMaple(DreamcastExecutableMemory& memory,
                   DreamcastSystemAsic& system_asic,
                   const ResolvedInputFrame& input) noexcept
        : memory_(&memory), system_asic_(&system_asic), input_(&input) {}

    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override {
        const auto physical = physical_address(address);
        if (register_byte(physical, kMdstar)) {
            return Result<std::uint8_t>::success(read_register_byte(mdstar_, physical));
        }
        if (register_byte(physical, kMdtsel)) {
            return Result<std::uint8_t>::success(read_register_byte(mdtsel_, physical));
        }
        if (register_byte(physical, kMden)) {
            return Result<std::uint8_t>::success(read_register_byte(mden_, physical));
        }
        if (register_byte(physical, kMdst)) {
            return Result<std::uint8_t>::success(read_register_byte(mdst_, physical));
        }
        return unsupported_read();
    }

    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override {
        const auto physical = physical_address(address);
        if (register_byte(physical, kMdstar)) {
            write_register_byte(mdstar_, physical, value);
            mdstar_ &= 0x0FFFFFE0u;
            return Result<void>::success();
        }
        if (register_byte(physical, kMdtsel)) {
            write_register_byte(mdtsel_, physical, value);
            mdtsel_ &= 1u;
            return Result<void>::success();
        }
        if (register_byte(physical, kMden)) {
            write_register_byte(mden_, physical, value);
            mden_ &= 1u;
            return Result<void>::success();
        }
        if (register_byte(physical, kMdst)) {
            if (physical != kMdst) {
                if (value == 0u) return Result<void>::success();
                return Result<void>::failure(
                    ErrorCode::invalid_argument,
                    "Dreamcast Maple MDST reserved bits are not supported");
            }
            if (value == 0u) return Result<void>::success();
            if (value != 1u) {
                return Result<void>::failure(
                    ErrorCode::invalid_argument,
                    "Dreamcast Maple MDST start value is invalid");
            }
            return start_dma();
        }
        return unsupported_write();
    }

private:
    struct StagedWrite {
        std::uint32_t address{};
        std::uint32_t value{};
        std::uint32_t old_value{};
    };

    static constexpr std::uint32_t kMapleBase = 0x005F6C00u;
    static constexpr std::uint32_t kMdstar = kMapleBase + 0x04u;
    static constexpr std::uint32_t kMdtsel = kMapleBase + 0x10u;
    static constexpr std::uint32_t kMden = kMapleBase + 0x14u;
    static constexpr std::uint32_t kMdst = kMapleBase + 0x18u;
    static constexpr std::uint32_t kDmaCompleteNormalEvent = 1u << 12u;
    static constexpr std::uint32_t kDescriptorFinal = 0x80000000u;
    static constexpr std::uint32_t kDescriptorReservedMask = 0x7FFCFF00u;
    static constexpr std::size_t kMaxDmaEntries = 256u;
    static constexpr std::uint32_t kControllerFunction = 0x00000001u;
    static constexpr std::uint32_t kStandardControllerCapabilities = 0xFE060F00u;

    [[nodiscard]] Result<void> start_dma() {
        if ((mden_ & 1u) == 0u) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "Dreamcast Maple DMA start requested while disabled");
        }
        if ((mdtsel_ & 1u) != 0u) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "Dreamcast Maple hardware-triggered DMA is not implemented");
        }
        if ((mdstar_ & 31u) != 0u) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "Dreamcast Maple DMA command table is not 32-byte aligned");
        }

        mdst_ = 1u;
        std::vector<StagedWrite> staged;
        std::uint32_t descriptor_address = mdstar_;
        bool found_final = false;

        for (std::size_t entry = 0u; entry < kMaxDmaEntries; ++entry) {
            const auto control = read_dreamcast_u32(*memory_, descriptor_address);
            if (!control) return dma_failure(control.error, "control word: " + control.detail);
            if ((control.value & kDescriptorReservedMask) != 0u) {
                return dma_failure(ErrorCode::invalid_argument,
                                   "descriptor contains unsupported control bits");
            }

            const auto receive_address_value = checked_add(descriptor_address, 4u);
            if (!receive_address_value) {
                return dma_failure(receive_address_value.error, receive_address_value.detail);
            }
            const auto frame_header_address = checked_add(descriptor_address, 8u);
            if (!frame_header_address) {
                return dma_failure(frame_header_address.error, frame_header_address.detail);
            }
            const auto receive_address = read_dreamcast_u32(*memory_, receive_address_value.value);
            if (!receive_address) {
                return dma_failure(receive_address.error,
                                   "receive address: " + receive_address.detail);
            }
            const auto frame_header = read_dreamcast_u32(*memory_, frame_header_address.value);
            if (!frame_header) {
                return dma_failure(frame_header.error,
                                   "frame header: " + frame_header.detail);
            }

            const auto port = static_cast<std::uint8_t>((control.value >> 16u) & 0x3u);
            const auto payload_words = static_cast<std::uint8_t>(control.value & 0xFFu);
            const auto frame_payload_words = static_cast<std::uint8_t>(frame_header.value & 0xFFu);
            if (frame_payload_words != payload_words) {
                return dma_failure(ErrorCode::invalid_argument,
                                   "frame length does not match descriptor payload count");
            }
            if ((receive_address.value & 3u) != 0u) {
                return dma_failure(ErrorCode::invalid_argument,
                                   "receive buffer is not 32-bit aligned");
            }

            const auto expected_destination = static_cast<std::uint8_t>(0x20u + port * 0x40u);
            const auto expected_source = static_cast<std::uint8_t>(port * 0x40u);
            const auto destination = static_cast<std::uint8_t>((frame_header.value >> 16u) & 0xFFu);
            const auto source = static_cast<std::uint8_t>((frame_header.value >> 8u) & 0xFFu);
            if (destination != expected_destination || source != expected_source) {
                return dma_failure(ErrorCode::invalid_argument,
                                   "frame addresses do not match descriptor port");
            }

            std::vector<std::uint32_t> payload;
            payload.reserve(payload_words);
            for (std::uint32_t i = 0u; i < payload_words; ++i) {
                const auto offset = 12u + i * 4u;
                const auto payload_address = checked_add(descriptor_address, offset);
                if (!payload_address) {
                    return dma_failure(payload_address.error, payload_address.detail);
                }
                const auto payload_word = read_dreamcast_u32(*memory_, payload_address.value);
                if (!payload_word) {
                    return dma_failure(payload_word.error,
                                       "payload word: " + payload_word.detail);
                }
                payload.push_back(payload_word.value);
            }

            const auto response = build_response(port, frame_header.value, payload);
            if (!response) return dma_failure(response.error, response.detail);
            for (std::size_t i = 0u; i < response.value.size(); ++i) {
                const auto response_address = checked_add(
                    receive_address.value, static_cast<std::uint32_t>(i * 4u));
                if (!response_address) {
                    return dma_failure(response_address.error, response_address.detail);
                }
                const auto staged_result = stage_write(
                    staged, response_address.value, response.value[i]);
                if (!staged_result) {
                    return dma_failure(staged_result.error, staged_result.detail);
                }
            }

            const auto entry_bytes = 12u + static_cast<std::uint32_t>(payload_words) * 4u;
            if ((control.value & kDescriptorFinal) != 0u) {
                found_final = true;
                break;
            }
            const auto next = checked_add(descriptor_address, entry_bytes);
            if (!next) return dma_failure(next.error, next.detail);
            descriptor_address = next.value;
        }

        if (!found_final) {
            return dma_failure(ErrorCode::invalid_argument,
                               "command table exceeds 256-entry runtime safety limit");
        }

        std::size_t committed = 0u;
        for (; committed < staged.size(); ++committed) {
            const auto write = write_dreamcast_u32(
                *memory_, staged[committed].address, staged[committed].value);
            if (!write) {
                for (std::size_t rollback = committed; rollback > 0u; --rollback) {
                    const auto& prior = staged[rollback - 1u];
                    (void)write_dreamcast_u32(*memory_, prior.address, prior.old_value);
                }
                return dma_failure(write.error, "receive buffer: " + write.detail);
            }
        }

        mdst_ = 0u;
        system_asic_->raise_normal(kDmaCompleteNormalEvent);
        return Result<void>::success();
    }

    [[nodiscard]] Result<std::vector<std::uint32_t>> build_response(
        std::uint8_t port,
        std::uint32_t request_header,
        const std::vector<std::uint32_t>& payload) const {
        if (port >= input_player_count) {
            return Result<std::vector<std::uint32_t>>::success({0xFFFFFFFFu});
        }

        const auto command = static_cast<std::uint8_t>(request_header >> 24u);
        if (command == 0x01u) {
            if (!payload.empty()) {
                return Result<std::vector<std::uint32_t>>::failure(
                    ErrorCode::invalid_argument,
                    "Device Request payload must be empty");
            }
            return Result<std::vector<std::uint32_t>>::success(
                make_device_info_response(request_header));
        }
        if (command == 0x09u) {
            if (payload.size() != 1u || payload[0] != kControllerFunction) {
                return Result<std::vector<std::uint32_t>>::failure(
                    ErrorCode::invalid_argument,
                    "Get Condition must request the controller function");
            }
            return Result<std::vector<std::uint32_t>>::success(
                make_condition_response(port, request_header));
        }
        return Result<std::vector<std::uint32_t>>::failure(
            ErrorCode::invalid_argument,
            "Maple command is outside the R2.3 controller endpoint scope");
    }

    [[nodiscard]] std::vector<std::uint32_t> make_device_info_response(
        std::uint32_t request_header) const {
        std::vector<std::uint32_t> response;
        response.reserve(29u);
        response.push_back(response_header(0x05u, request_header, 28u));

        std::array<std::uint8_t, 112u> info{};
        store_word(info, 0u, kControllerFunction);
        store_word(info, 4u, kStandardControllerCapabilities);
        info[16u] = 0xFFu;
        info[17u] = 0x00u;
        std::fill(info.begin() + 18, info.begin() + 48, static_cast<std::uint8_t>(' '));
        std::fill(info.begin() + 48, info.begin() + 108, static_cast<std::uint8_t>(' '));
        copy_ascii(info, 18u, 30u, "JOJO Recompiled Controller");
        copy_ascii(info, 48u, 60u, "JOJO Recompiled");

        for (std::size_t offset = 0u; offset < info.size(); offset += 4u) {
            const auto word = static_cast<std::uint32_t>(info[offset + 0u]) |
                              (static_cast<std::uint32_t>(info[offset + 1u]) << 8u) |
                              (static_cast<std::uint32_t>(info[offset + 2u]) << 16u) |
                              (static_cast<std::uint32_t>(info[offset + 3u]) << 24u);
            response.push_back(word);
        }
        return response;
    }

    [[nodiscard]] std::vector<std::uint32_t> make_condition_response(
        std::uint8_t port,
        std::uint32_t request_header) const {
        const ResolvedPlayerInput neutral{};
        const auto& player = input_ != nullptr ? (*input_)[port] : neutral;

        std::uint16_t pressed = 0u;
        if (player.pressed(GameAction::attack_heavy)) pressed |= 1u << 1u; // B
        if (player.pressed(GameAction::stand)) pressed |= 1u << 2u; // A
        if (player.pressed(GameAction::start) || player.pressed(GameAction::pause)) pressed |= 1u << 3u;
        if (player.pressed(GameAction::up)) pressed |= 1u << 4u;
        if (player.pressed(GameAction::down)) pressed |= 1u << 5u;
        if (player.pressed(GameAction::left)) pressed |= 1u << 6u;
        if (player.pressed(GameAction::right)) pressed |= 1u << 7u;
        if (player.pressed(GameAction::attack_medium)) pressed |= 1u << 9u; // Y
        if (player.pressed(GameAction::attack_light)) pressed |= 1u << 10u; // X

        const auto raw_buttons = static_cast<std::uint16_t>(~pressed);
        const auto first_data = static_cast<std::uint32_t>(raw_buttons) << 16u;
        const auto joy_x = digital_axis(player.pressed(GameAction::left),
                                        player.pressed(GameAction::right));
        const auto joy_y = digital_axis(player.pressed(GameAction::up),
                                        player.pressed(GameAction::down));
        const auto second_data = 0x00008080u |
                                 (static_cast<std::uint32_t>(joy_y) << 16u) |
                                 (static_cast<std::uint32_t>(joy_x) << 24u);

        return {
            response_header(0x08u, request_header, 3u),
            kControllerFunction,
            first_data,
            second_data,
        };
    }

    [[nodiscard]] Result<void> stage_write(std::vector<StagedWrite>& staged,
                                           std::uint32_t address,
                                           std::uint32_t value) const {
        if ((address & 3u) != 0u) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "receive buffer is not 32-bit aligned");
        }
        const auto duplicate = std::find_if(
            staged.begin(), staged.end(),
            [address](const StagedWrite& write) { return write.address == address; });
        if (duplicate != staged.end()) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "receive buffers overlap within one DMA chain");
        }
        const auto old = read_dreamcast_u32(*memory_, address);
        if (!old) {
            return Result<void>::failure(old.error,
                                         "receive buffer: " + old.detail);
        }
        staged.push_back({address, value, old.value});
        return Result<void>::success();
    }

    [[nodiscard]] static Result<std::uint32_t> checked_add(
        std::uint32_t base,
        std::uint32_t offset) {
        const auto sum = static_cast<std::uint64_t>(base) + offset;
        if (sum > std::numeric_limits<std::uint32_t>::max()) {
            return Result<std::uint32_t>::failure(
                ErrorCode::invalid_argument,
                "command table address arithmetic overflowed");
        }
        return Result<std::uint32_t>::success(static_cast<std::uint32_t>(sum));
    }

    [[nodiscard]] static std::uint32_t response_header(
        std::uint8_t command,
        std::uint32_t request_header,
        std::uint8_t payload_words) noexcept {
        const auto request_destination = static_cast<std::uint8_t>((request_header >> 16u) & 0xFFu);
        const auto request_source = static_cast<std::uint8_t>((request_header >> 8u) & 0xFFu);
        return (static_cast<std::uint32_t>(command) << 24u) |
               (static_cast<std::uint32_t>(request_source) << 16u) |
               (static_cast<std::uint32_t>(request_destination) << 8u) |
               payload_words;
    }

    [[nodiscard]] static std::uint8_t digital_axis(bool negative,
                                                   bool positive) noexcept {
        if (negative == positive) return 128u;
        return negative ? 0u : 255u;
    }

    static void store_word(std::array<std::uint8_t, 112u>& target,
                           std::size_t offset,
                           std::uint32_t value) noexcept {
        target[offset + 0u] = static_cast<std::uint8_t>(value);
        target[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        target[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
        target[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    }

    static void copy_ascii(std::array<std::uint8_t, 112u>& target,
                           std::size_t offset,
                           std::size_t width,
                           std::string_view text) noexcept {
        const auto count = std::min(width, text.size());
        for (std::size_t i = 0u; i < count; ++i) {
            target[offset + i] = static_cast<std::uint8_t>(text[i]);
        }
    }

    [[nodiscard]] Result<void> dma_failure(ErrorCode error, const std::string& detail) {
        mdst_ = 0u;
        return Result<void>::failure(error, "Dreamcast Maple DMA " + detail);
    }

    [[nodiscard]] static std::uint32_t physical_address(std::uint32_t address) noexcept {
        if (address >= 0x80000000u && address < 0xE0000000u) {
            return address & 0x1FFFFFFFu;
        }
        return address;
    }

    [[nodiscard]] static bool register_byte(std::uint32_t address,
                                            std::uint32_t base) noexcept {
        return address >= base && address <= base + 3u;
    }

    [[nodiscard]] static std::uint8_t read_register_byte(std::uint32_t value,
                                                         std::uint32_t address) noexcept {
        const auto shift = static_cast<unsigned>((address & 3u) * 8u);
        return static_cast<std::uint8_t>((value >> shift) & 0xFFu);
    }

    static void write_register_byte(std::uint32_t& target,
                                    std::uint32_t address,
                                    std::uint8_t value) noexcept {
        const auto shift = static_cast<unsigned>((address & 3u) * 8u);
        const auto byte_mask = static_cast<std::uint32_t>(0xFFu) << shift;
        target = (target & ~byte_mask) | (static_cast<std::uint32_t>(value) << shift);
    }

    [[nodiscard]] static Result<std::uint8_t> unsupported_read() {
        return Result<std::uint8_t>::failure(
            ErrorCode::invalid_argument,
            "Dreamcast Maple register is not implemented");
    }

    [[nodiscard]] static Result<void> unsupported_write() {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "Dreamcast Maple register is not implemented");
    }

    DreamcastExecutableMemory* memory_{};
    DreamcastSystemAsic* system_asic_{};
    const ResolvedInputFrame* input_{};
    std::uint32_t mdstar_{};
    std::uint32_t mdtsel_{};
    std::uint32_t mden_{};
    std::uint32_t mdst_{};
};

}
