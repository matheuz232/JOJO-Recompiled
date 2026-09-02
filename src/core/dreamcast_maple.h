#pragma once

#include "core/dreamcast_bus.h"

#include <cstdint>

namespace jojo {

class DreamcastMaple final : public DreamcastMmioDevice {
public:
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
        return unsupported_write();
    }

private:
    static constexpr std::uint32_t kMapleBase = 0x005F6C00u;
    static constexpr std::uint32_t kMdstar = kMapleBase + 0x04u;
    static constexpr std::uint32_t kMdtsel = kMapleBase + 0x10u;
    static constexpr std::uint32_t kMden = kMapleBase + 0x14u;

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

    std::uint32_t mdstar_{};
    std::uint32_t mdtsel_{};
    std::uint32_t mden_{};
};

}
