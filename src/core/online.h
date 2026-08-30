#pragma once

#include "core/mod_runtime.h"
#include "core/network_protocol.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

enum class OnlineMode : std::uint8_t {
    casual,
    ranked,
    direct,
    custom,
};

struct OnlineMenuEntry {
    OnlineMode mode{OnlineMode::casual};
    std::string title;
    std::string description;

    friend bool operator==(const OnlineMenuEntry&, const OnlineMenuEntry&) = default;
};

[[nodiscard]] const std::vector<OnlineMenuEntry>& default_online_menu_entries();

struct MatchRules {
    std::uint8_t rounds_to_win{2};
    bool timer_enabled{true};
    std::uint16_t timer_seconds{99};

    friend bool operator==(const MatchRules&, const MatchRules&) = default;
};

struct OnlineNetworkSettings {
    std::uint32_t max_rollback_frames{8};
    std::uint32_t input_delay_frames{};
    bool show_telemetry{true};

    friend bool operator==(const OnlineNetworkSettings&, const OnlineNetworkSettings&) = default;
};

[[nodiscard]] Result<void> validate_match_rules(const MatchRules& rules);
[[nodiscard]] Result<void> validate_online_network_settings(const OnlineNetworkSettings& settings);

enum class OnlineModPolicyKind : std::uint8_t {
    ranked_legal_only,
    exact_mod_set,
    unrestricted,
};

struct OnlineModPolicy {
    OnlineModPolicyKind kind{OnlineModPolicyKind::exact_mod_set};
    std::string required_mod_set_hash;

    friend bool operator==(const OnlineModPolicy&, const OnlineModPolicy&) = default;
};

[[nodiscard]] Result<ModSessionPolicy> make_mod_session_policy(
    OnlineMode mode,
    const OnlineModPolicy& online_policy,
    const ModSetHashes& local_hashes);

struct OnlineMatchRequest {
    OnlineMode mode{OnlineMode::casual};
    MatchRules rules{};
    OnlineNetworkSettings network{};
    OnlineModPolicy mod_policy{};

    friend bool operator==(const OnlineMatchRequest&, const OnlineMatchRequest&) = default;
};

struct DirectRoomDescriptor {
    std::string room_id;
    std::string invite_code;
    MatchRules rules{};
    OnlineNetworkSettings network{};
    OnlineModPolicy mod_policy{};

    friend bool operator==(const DirectRoomDescriptor&, const DirectRoomDescriptor&) = default;
};

class IOnlineBackend {
public:
    virtual ~IOnlineBackend() = default;

    [[nodiscard]] virtual Result<void> start_matchmaking(const OnlineMatchRequest& request) = 0;
    [[nodiscard]] virtual Result<void> cancel_matchmaking() = 0;
    [[nodiscard]] virtual Result<DirectRoomDescriptor> create_direct_room(
        const OnlineMatchRequest& request) = 0;
    [[nodiscard]] virtual Result<void> join_direct_room(std::string_view invite_code) = 0;
};

class OnlineProductController {
public:
    OnlineProductController(
        IOnlineBackend& backend,
        const ResolvedModSet& mods,
        ModSetHashes hashes) noexcept;

    [[nodiscard]] Result<void> begin_matchmaking(const OnlineMatchRequest& request);
    [[nodiscard]] Result<void> cancel_matchmaking();
    [[nodiscard]] Result<DirectRoomDescriptor> create_direct_room(const OnlineMatchRequest& request);
    [[nodiscard]] Result<void> join_direct_room(std::string_view invite_code);
    [[nodiscard]] Result<void> validate_room(const DirectRoomDescriptor& room) const;

private:
    [[nodiscard]] Result<void> validate_request(const OnlineMatchRequest& request) const;

    IOnlineBackend& backend_;
    const ResolvedModSet& mods_;
    ModSetHashes hashes_;
};

enum class OnlineConnectionLifecycle : std::uint8_t {
    connected,
    reconnecting,
    disconnected,
    left,
};

enum class ConnectionQuality : std::uint8_t {
    excellent,
    good,
    fair,
    poor,
    unusable,
};

struct OnlineConnectionMetrics {
    NetworkTelemetry network{};
    std::uint64_t observed_frames{};
};

struct ConnectionIndicator {
    ConnectionQuality quality{ConnectionQuality::unusable};
    OnlineConnectionLifecycle lifecycle{OnlineConnectionLifecycle::disconnected};
    std::uint8_t signal_bars{};
    std::string signal_token;
    std::string text;

    friend bool operator==(const ConnectionIndicator&, const ConnectionIndicator&) = default;
};

[[nodiscard]] OnlineConnectionLifecycle online_lifecycle_from_network_state(
    NetworkConnectionState state) noexcept;
[[nodiscard]] ConnectionQuality evaluate_connection_quality(
    const OnlineConnectionMetrics& metrics) noexcept;
[[nodiscard]] ConnectionIndicator make_connection_indicator(
    const OnlineConnectionMetrics& metrics,
    OnlineConnectionLifecycle lifecycle);

enum class MatchOutcome : std::uint8_t {
    win,
    loss,
    draw,
    disconnected,
    left,
};

struct OnlineProfile {
    std::string player_id;
    std::string display_name;
    std::int32_t ranked_rating{1000};
    std::uint64_t casual_wins{};
    std::uint64_t casual_losses{};
    std::uint64_t ranked_wins{};
    std::uint64_t ranked_losses{};

    friend bool operator==(const OnlineProfile&, const OnlineProfile&) = default;
};

struct MatchHistoryEntry {
    std::string match_id;
    OnlineMode mode{OnlineMode::casual};
    MatchOutcome outcome{MatchOutcome::draw};
    std::string opponent_name;
    std::uint8_t local_rounds{};
    std::uint8_t remote_rounds{};
    std::string replay_id;
    ConnectionQuality quality{ConnectionQuality::unusable};

    friend bool operator==(const MatchHistoryEntry&, const MatchHistoryEntry&) = default;
};

class OnlineMatchHistory {
public:
    explicit OnlineMatchHistory(std::size_t capacity) noexcept : capacity_(capacity) {}

    [[nodiscard]] Result<void> append(MatchHistoryEntry entry);
    [[nodiscard]] const std::deque<MatchHistoryEntry>& entries() const noexcept { return entries_; }

private:
    std::size_t capacity_{};
    std::deque<MatchHistoryEntry> entries_;
};

} // namespace jojo
