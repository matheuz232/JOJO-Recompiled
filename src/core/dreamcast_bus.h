#pragma once

#include "core/dreamcast_memory.h"
#include "core/sh4_reference_executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace jojo {

enum class DreamcastBusRegion {
    main_ram,
    system_asic,
    maple,
    gdrom_g1,
    pvr_registers,
    pvr_vram,
    pvr_ta,
    aica_registers,
    aica_wave_ram,
    sh4_internal,
    unknown,
};

enum class DreamcastBusAccess {
    read,
    write,
};

struct DreamcastBusFault {
    std::uint32_t address{};
    DreamcastBusRegion region{DreamcastBusRegion::unknown};
    DreamcastBusAccess access{DreamcastBusAccess::read};
    std::uint8_t width_bytes{1u};
};

class DreamcastMmioDevice {
public:
    virtual ~DreamcastMmioDevice() = default;
    [[nodiscard]] virtual Result<std::uint8_t> read8(std::uint32_t address) = 0;
    [[nodiscard]] virtual Result<void> write8(std::uint32_t address, std::uint8_t value) = 0;
};

[[nodiscard]] DreamcastBusRegion classify_dreamcast_bus_region(
    std::uint32_t address) noexcept;

class DreamcastReferenceBus final : public Sh4ReferenceBus {
public:
    explicit DreamcastReferenceBus(DreamcastExecutableMemory& memory) noexcept
        : memory_(&memory) {}

    void attach_device(DreamcastBusRegion region, DreamcastMmioDevice& device) noexcept;
    void detach_device(DreamcastBusRegion region) noexcept;

    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override;
    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override;

    [[nodiscard]] const std::optional<DreamcastBusFault>& last_fault() const noexcept {
        return last_fault_;
    }
    void clear_fault() noexcept { last_fault_.reset(); }

private:
    static constexpr std::size_t kRegionSlots =
        static_cast<std::size_t>(DreamcastBusRegion::unknown) + 1u;

    [[nodiscard]] DreamcastMmioDevice* device_for(DreamcastBusRegion region) const noexcept;
    void record_fault(std::uint32_t address, DreamcastBusAccess access) noexcept;

    DreamcastExecutableMemory* memory_{};
    std::array<DreamcastMmioDevice*, kRegionSlots> devices_{};
    std::optional<DreamcastBusFault> last_fault_;
};

}
