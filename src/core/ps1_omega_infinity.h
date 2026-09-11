#pragma once

#include "core/ps1_exe.h"
#include "core/result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace jojo {

struct Ps1OmegaInfinityOptions {
    std::uint64_t epoch_retired_limit{3000000000ull};
    std::uint64_t instruction_quantum{1000000ull};
    std::uint64_t chunk_target_bytes{8ull * 1024ull * 1024ull};
    std::uint64_t max_session_disk_bytes{16ull * 1024ull * 1024ull * 1024ull};
    std::size_t hot_trace_capacity{262144u};
    bool stop_on_strict_commercial_frame{true};
};

enum class Ps1OmegaInfinityStopReason : std::uint8_t {
    none,
    user_requested,
    strict_commercial_frame,
    fatal_no_safe_continuation,
    disk_budget_exhausted,
    invalid_resume_state,
};

struct Ps1OmegaInfinitySummary {
    std::uint64_t epoch_count{};
    std::uint64_t total_retired{};
    std::uint64_t strict_frontier_count{};
    std::uint64_t speculative_frontier_count{};
    std::uint64_t unique_state_count{};
    std::uint64_t presented_frames{};
    Ps1OmegaInfinityStopReason stop_reason{Ps1OmegaInfinityStopReason::none};
    std::filesystem::path session_root;
};

struct Ps1OmegaInfinityProgress {
    std::uint64_t epoch{};
    std::uint64_t epoch_retired{};
    std::uint64_t total_retired{};
    std::uint64_t strict_frontier_count{};
    std::uint64_t speculative_frontier_count{};
    std::uint64_t committed_disk_bytes{};
    std::optional<std::uint32_t> latest_strict_pc;
    std::optional<std::uint32_t> latest_strict_address;
};

using Ps1OmegaInfinityProgressCallback =
    std::function<void(const Ps1OmegaInfinityProgress&)>;

class Ps1OmegaInfinityControl {
public:
    void request_stop() noexcept {
        stop_requested_.store(true, std::memory_order_relaxed);
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_requested_.load(std::memory_order_relaxed);
    }

private:
    std::atomic_bool stop_requested_{false};
};

[[nodiscard]] constexpr Ps1OmegaInfinityOptions ps1_omega_infinity_options() noexcept {
    return Ps1OmegaInfinityOptions{};
}

[[nodiscard]] constexpr bool ps1_omega_infinity_epoch_complete(
    std::uint64_t epoch_retired,
    const Ps1OmegaInfinityOptions& options) noexcept {
    return epoch_retired >= options.epoch_retired_limit;
}

[[nodiscard]] Result<Ps1OmegaInfinitySummary> explore_ps1_omega_infinity(
    const Ps1Executable& executable,
    const std::filesystem::path& session_root,
    const Ps1OmegaInfinityOptions& options,
    Ps1OmegaInfinityControl& control,
    Ps1OmegaInfinityProgressCallback progress = {});

[[nodiscard]] bool ps1_omega_infinity_has_resumable_session(
    const std::filesystem::path& session_root);

[[nodiscard]] bool ps1_omega_infinity_has_compatible_resumable_session(
    const std::filesystem::path& session_root,
    std::string_view executable_identity);

} // namespace jojo