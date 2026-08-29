#pragma once

#include "core/dreamcast_memory.h"
#include "core/sh4_reference_executor.h"

namespace jojo {

class DreamcastReferenceBus final : public Sh4ReferenceBus {
public:
    explicit DreamcastReferenceBus(DreamcastExecutableMemory& memory) noexcept
        : memory_(&memory) {}

    [[nodiscard]] Result<std::uint8_t> read8(std::uint32_t address) override;
    [[nodiscard]] Result<void> write8(std::uint32_t address, std::uint8_t value) override;

private:
    DreamcastExecutableMemory* memory_{};
};

}
