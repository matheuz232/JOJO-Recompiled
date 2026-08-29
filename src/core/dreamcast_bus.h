#pragma once

#include "core/dreamcast_memory.h"
#include "core/sh4_reference_executor.h"

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

[[nodiscard]] DreamcastBusRegion classify_dreamcast_bus_region(
    std::uint32_t address) noexcept;

class DreamcastReferenceBus final : public Sh4ReferenceBus {
public:
    explicit DreamcastReferenceBus(DreamcastExecutableMemory& memory) noexcept
        : memory_(&memory) {}

    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override;
    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override;

    [[nodiscard]] const std::optional<DreamcastBusFault>& last_fault() const noexcept {
        return last_fault_;
    }
    void clear_fault() noexcept { last_fault_.reset(); }

private:
    void record_fault(std::uint32_t address, DreamcastBusAccess access) noexcept;

    DreamcastExecutableMemory* memory_{};
    std::optional<DreamcastBusFault> last_fault_;
};

}
