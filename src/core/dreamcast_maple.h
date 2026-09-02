#pragma once

#include "core/dreamcast_bus.h"
#include "core/dreamcast_memory.h"
#include "core/dreamcast_system_asic.h"

#include <cstdint>
#include <string>

namespace jojo {

class DreamcastMaple final : public DreamcastMmioDevice {
public:
    DreamcastMaple(DreamcastExecutableMemory& memory,
                   DreamcastSystemAsic& system_asic) noexcept
        : memory_(&memory), system_asic_(&system_asic) {}

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
    static constexpr std::uint32_t kMapleBase = 0x005F6C00u;
    static constexpr std::uint32_t kMdstar = kMapleBase + 0x04u;
    static constexpr std::uint32_t kMdtsel = kMapleBase + 0x10u;
    static constexpr std::uint32_t kMden = kMapleBase + 0x14u;
    static constexpr std::uint32_t kMdst = kMapleBase + 0x18u;
    static constexpr std::uint32_t kDmaCompleteNormalEvent = 1u << 12u;
    static constexpr std::uint32_t kDescriptorFinal = 0x80000000u;
    static constexpr std::uint32_t kDescriptorReservedMask = 0x7FFCFF00u;

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

        const auto control = read_dreamcast_u32(*memory_, mdstar_);
        if (!control) return dma_failure(control.error, "control word: " + control.detail);
        if ((control.value & kDescriptorFinal) == 0u) {
            return dma_failure(ErrorCode::invalid_argument,
                               "multi-entry command tables are not implemented");
        }
        if ((control.value & kDescriptorReservedMask) != 0u) {
            return dma_failure(ErrorCode::invalid_argument,
                               "descriptor contains unsupported control bits");
        }

        const auto receive_address = read_dreamcast_u32(*memory_, mdstar_ + 4u);
        if (!receive_address) {
            return dma_failure(receive_address.error,
                               "receive address: " + receive_address.detail);
        }
        const auto frame_header = read_dreamcast_u32(*memory_, mdstar_ + 8u);
        if (!frame_header) {
            return dma_failure(frame_header.error,
                               "frame header: " + frame_header.detail);
        }

        const auto port = static_cast<std::uint8_t>((control.value >> 16u) & 0x3u);
        const auto payload_words = static_cast<std::uint8_t>(control.value & 0xFFu);
        const auto frame_payload_words =
            static_cast<std::uint8_t>(frame_header.value & 0xFFu);
        (void)port;

        if (frame_payload_words != payload_words) {
            return dma_failure(
                ErrorCode::invalid_argument,
                "frame length does not match descriptor payload count");
        }

        for (std::uint32_t i = 0u; i < payload_words; ++i) {
            const auto payload_address = mdstar_ + 12u + i * 4u;
            const auto payload = read_dreamcast_u32(*memory_, payload_address);
            if (!payload) {
                return dma_failure(payload.error,
                                   "payload word: " + payload.detail);
            }
        }

        if ((receive_address.value & 3u) != 0u) {
            return dma_failure(ErrorCode::invalid_argument,
                               "receive buffer is not 32-bit aligned");
        }
        const auto response = write_dreamcast_u32(*memory_,
                                                   receive_address.value,
                                                   0xFFFFFFFFu);
        if (!response) {
            return dma_failure(response.error,
                               "receive buffer: " + response.detail);
        }

        mdst_ = 0u;
        system_asic_->raise_normal(kDmaCompleteNormalEvent);
        return Result<void>::success();
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
    std::uint32_t mdstar_{};
    std::uint32_t mdtsel_{};
    std::uint32_t mden_{};
    std::uint32_t mdst_{};
};

}
