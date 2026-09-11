#pragma once

#include "core/ps1_omega_infinity.h"
#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace jojo {

struct Ps1OmegaBundleInfo {
    std::filesystem::path summary_path;
    std::filesystem::path manifest_path;
    std::uint64_t strict_frontier_count{};
    std::uint64_t speculative_frontier_count{};
    std::uint64_t unique_serialized_state_count{};
    std::uint64_t coverage_unique_pc_count{};
    std::uint64_t coverage_unique_edge_count{};
    std::uint64_t coverage_unique_mmio_count{};
};

[[nodiscard]] std::string ps1_omega_infinity_stop_reason_name(
    Ps1OmegaInfinityStopReason reason) noexcept;

[[nodiscard]] Result<Ps1OmegaBundleInfo> finalize_ps1_omega_bundle(
    const std::filesystem::path& session_root,
    std::string_view executable_identity,
    const Ps1OmegaInfinitySummary& run_summary);

[[nodiscard]] Result<std::filesystem::path> package_ps1_omega_bundle_zip(
    const std::filesystem::path& session_root,
    const std::filesystem::path& output_zip,
    std::function<bool()> cancel = {});

} // namespace jojo