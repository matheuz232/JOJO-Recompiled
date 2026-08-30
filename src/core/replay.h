#pragma once

#include "core/online.h"
#include "core/result.h"
#include "core/rollback.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace jojo {

inline constexpr std::uint16_t kReplayFormatVersion = 1;

struct ReplayFrame {
    std::uint64_t frame{};
    RollbackInput local{};
    RollbackInput remote{};
    std::string state_hash_hex;

    friend bool operator==(const ReplayFrame&, const ReplayFrame&) = default;
};

struct OnlineReplay {
    std::string replay_id;
    OnlineMode mode{OnlineMode::casual};
    std::uint64_t rng_seed{};
    std::string initial_state_hash_hex;
    std::string mod_set_hash;
    std::vector<ReplayFrame> frames;

    friend bool operator==(const OnlineReplay&, const OnlineReplay&) = default;
};

[[nodiscard]] Result<void> validate_online_replay(const OnlineReplay& replay);
[[nodiscard]] Result<std::vector<std::uint8_t>> serialize_online_replay(const OnlineReplay& replay);
[[nodiscard]] Result<OnlineReplay> parse_online_replay(std::span<const std::uint8_t> bytes);

} // namespace jojo
