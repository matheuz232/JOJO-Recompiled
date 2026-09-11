#pragma once

#include "core/ps1_max3_explorer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <tuple>

namespace jojo {

struct Ps1Max3CoverageDelta {
    std::size_t new_states{};
    std::size_t new_pcs{};
    std::size_t new_opcodes{};
    std::size_t new_bios_pairs{};
    std::size_t new_mmio_tuples{};
    std::size_t new_cd_contexts{};
    std::size_t new_cpu_classes{};
    std::size_t new_frontiers{};
    std::size_t new_landmarks{};
};

struct Ps1Max3ProgressLandmarks {
    std::uint64_t max_interrupts_accepted{};
    std::uint64_t max_cdrom_command_count{};
    std::uint64_t max_dma_transfer_count{};
    std::uint64_t max_gpu_gp0_command_count{};
    std::uint64_t max_gpu_gp1_command_count{};
    std::uint64_t max_vram_write_count{};
    std::uint64_t max_presented_frames{};
};

class Ps1Max3Coverage {
public:
    [[nodiscard]] Ps1Max3CoverageDelta observe(
        const Ps1BootReport& report,
        const Ps1Max3Frontier* frontier,
        std::optional<std::uint64_t> state_hash);

    [[nodiscard]] const Ps1Max3ProgressLandmarks& landmarks() const noexcept;
    [[nodiscard]] std::uint64_t deterministic_hash() const noexcept;

private:
    using BiosKey = std::tuple<std::uint32_t, std::uint32_t>;
    using MmioKey = std::tuple<std::uint32_t, std::uint8_t, bool>;
    using CdKey = std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>;
    using CpuKey = std::tuple<std::uint8_t, std::uint8_t, bool, std::uint8_t>;
    using FrontierKey = std::tuple<
        std::uint8_t,
        std::uint32_t,
        bool,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint8_t,
        bool,
        std::uint32_t>;

    std::set<std::uint64_t> states_;
    std::set<std::uint32_t> pcs_;
    std::set<std::uint32_t> opcodes_;
    std::set<BiosKey> bios_pairs_;
    std::set<MmioKey> mmio_tuples_;
    std::set<CdKey> cd_contexts_;
    std::set<CpuKey> cpu_classes_;
    std::set<FrontierKey> frontiers_;

    bool saw_interrupt_{};
    bool saw_cdrom_{};
    bool saw_dma_{};
    bool saw_gp0_{};
    bool saw_gp1_{};
    bool saw_vram_{};
    bool saw_frame_{};
    Ps1Max3ProgressLandmarks landmarks_{};
};

} // namespace jojo
