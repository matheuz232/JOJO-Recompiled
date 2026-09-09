#pragma once

#include "core/r3000a_bus.h"
#include "core/result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jojo {

struct Ps1UnsupportedAccess {
    std::uint32_t guest_address{};
    std::uint32_t physical_address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
};

class Ps1MemoryBus final : public R3000aBus {
public:
    static constexpr std::uint32_t main_ram_size = 2u * 1024u * 1024u;
    static constexpr std::uint32_t scratchpad_base = 0x1F800000u;
    static constexpr std::uint32_t scratchpad_size = 1024u;

    Ps1MemoryBus();

    static std::optional<std::uint32_t> guest_to_physical(std::uint32_t guest) noexcept;

    R3000aBusResult read8(std::uint32_t address) noexcept override;
    R3000aBusResult read16(std::uint32_t address) noexcept override;
    R3000aBusResult read32(std::uint32_t address) noexcept override;
    R3000aBusResult write8(std::uint32_t address, std::uint8_t value) noexcept override;
    R3000aBusResult write16(std::uint32_t address, std::uint16_t value) noexcept override;
    R3000aBusResult write32(std::uint32_t address, std::uint32_t value) noexcept override;

    Result<void> load_main_ram(std::uint32_t guest_address,
                               std::span<const std::uint8_t> bytes);

    const std::optional<Ps1UnsupportedAccess>& last_unsupported_access() const noexcept;
    void clear_last_unsupported_access() noexcept;

private:
    std::vector<std::uint8_t> main_ram_;
    std::array<std::uint8_t, scratchpad_size> scratchpad_{};
    std::optional<Ps1UnsupportedAccess> last_unsupported_{};
};

} // namespace jojo
