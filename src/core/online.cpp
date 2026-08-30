#include "core/online.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

namespace jojo {

namespace {

bool blank(std::string_view text) {
    return text.empty() || std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

bool valid_online_mode(OnlineMode mode) noexcept {
    switch (mode) {
        case OnlineMode::casual:
        case OnlineMode::ranked:
        case OnlineMode::direct:
        case OnlineMode::custom:
            return true;
    }
    return false;
}

int rtt_impairment(double value) noexcept {
    if (value <= 60.0) return 0;
    if (value <= 100.0) return 1;
    if (value <= 150.0) return 2;
    if (value <= 220.0) return 3;
    return 4;
}

int jitter_impairment(double value) noexcept {
    if (value <= 5.0) return 0;
    if (value <= 12.0) return 1;
    if (value <= 25.0) return 2;
    if (value <= 45.0) return 3;
    return 4;
}

int loss_impairment(double value) noexcept {
    if (value <= 0.5) return 0;
    if (value <= 1.5) return 1;
    if (value <= 3.0) return 2;
    if (value <= 6.0) return 3;
    return 4;
}

int prediction_impairment(double value) noexcept {
    if (value <= 1.0) return 0;
    if (value <= 3.0) return 1;
    if (value <= 7.0) return 2;
    if (value <= 15.0) return 3;
    return 4;
}

int rollback_impairment(std::uint32_t value) noexcept {
    if (value <= 1) return 0;
    if (value <= 2) return 1;
    if (value <= 4) return 2;
    if (value <= 6) return 3;
    return 4;
}

std::pair<std::uint8_t, std::string> signal_for_quality(ConnectionQuality quality) {
    switch (quality) {
        case ConnectionQuality::excellent: return {4, "Excellent"};
        case ConnectionQuality::good: return {3, "Good"};
        case ConnectionQuality::fair: return {2, "Fair"};
        case ConnectionQuality::poor: return {1, "Poor"};
        case ConnectionQuality::unusable: return {0, "Unusable"};
    }
    return {0, "Unusable"};
}

} // namespace

const std::vector<OnlineMenuEntry>& default_online_menu_entries() {
    static const std::vector<OnlineMenuEntry> entries{
        {OnlineMode::casual, "Casual", "Find a casual opponent with deterministic peer compatibility."},
        {OnlineMode::ranked, "Ranked", "Competitive matchmaking using ranked-legal mod policy."},
        {OnlineMode::direct, "Direct 1v1", "Create or join an invite-based direct room."},
        {OnlineMode::custom, "Custom", "Search or host matches with explicit rules and mod policy."},
    };
    return entries;
}

Result<void> validate_match_rules(const MatchRules& rules) {
    if (rules.rounds_to_win < 1 || rules.rounds_to_win > 5) {
        return Result<void>::failure(ErrorCode::invalid_argument, "rounds_to_win must be 1..5");
    }
    if (rules.timer_enabled && (rules.timer_seconds < 30 || rules.timer_seconds > 999)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "enabled timer must be 30..999 seconds");
    }
    return Result<void>::success();
}

Result<void> validate_online_network_settings(const OnlineNetworkSettings& settings) {
    if (settings.max_rollback_frames < 1 || settings.max_rollback_frames > 20) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "max rollback frames must be 1..20");
    }
    if (settings.input_delay_frames > 8) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "input delay frames must be 0..8");
    }
    return Result<void>::success();
}

Result<ModSessionPolicy> make_mod_session_policy(
    OnlineMode mode,
    const OnlineModPolicy& online_policy,
    const ModSetHashes& local_hashes) {
    if (!valid_online_mode(mode)) {
        return Result<ModSessionPolicy>::failure(
            ErrorCode::invalid_argument,
            "unknown online mode");
    }

    if (mode == OnlineMode::ranked) {
        if (online_policy.kind != OnlineModPolicyKind::ranked_legal_only) {
            return Result<ModSessionPolicy>::failure(
                ErrorCode::invalid_argument,
                "ranked requires ranked_legal_only mod policy");
        }
        return Result<ModSessionPolicy>::success(ModSessionPolicy{ModSessionMode::ranked, {}});
    }

    if (mode == OnlineMode::casual && online_policy.kind != OnlineModPolicyKind::exact_mod_set) {
        return Result<ModSessionPolicy>::failure(
            ErrorCode::invalid_argument,
            "casual requires exact mod-set compatibility");
    }

    switch (online_policy.kind) {
        case OnlineModPolicyKind::ranked_legal_only:
            return Result<ModSessionPolicy>::success(ModSessionPolicy{ModSessionMode::ranked, {}});

        case OnlineModPolicyKind::exact_mod_set: {
            const auto required = online_policy.required_mod_set_hash.empty()
                ? local_hashes.mod_set_hash
                : online_policy.required_mod_set_hash;
            if (required.empty()) {
                return Result<ModSessionPolicy>::failure(
                    ErrorCode::invalid_argument,
                    "exact mod-set policy requires a non-empty mod-set hash");
            }
            return Result<ModSessionPolicy>::success(
                ModSessionPolicy{ModSessionMode::custom, required});
        }

        case OnlineModPolicyKind::unrestricted:
            return Result<ModSessionPolicy>::success(ModSessionPolicy{ModSessionMode::custom, {}});
    }

    return Result<ModSessionPolicy>::failure(ErrorCode::invalid_argument, "unknown online mod policy");
}

OnlineProductController::OnlineProductController(
    IOnlineBackend& backend,
    const ResolvedModSet& mods,
    ModSetHashes hashes) noexcept
    : backend_(backend), mods_(mods), hashes_(std::move(hashes)) {}

Result<void> OnlineProductController::validate_request(const OnlineMatchRequest& request) const {
    const auto rules = validate_match_rules(request.rules);
    if (!rules) return rules;

    const auto network = validate_online_network_settings(request.network);
    if (!network) return network;

    const auto policy = make_mod_session_policy(request.mode, request.mod_policy, hashes_);
    if (!policy) {
        return Result<void>::failure(policy.error, policy.detail);
    }
    return validate_mod_session(mods_, hashes_, policy.value);
}

Result<void> OnlineProductController::begin_matchmaking(const OnlineMatchRequest& request) {
    if (request.mode == OnlineMode::direct) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "direct mode uses invite rooms rather than matchmaking");
    }
    const auto validated = validate_request(request);
    if (!validated) return validated;
    return backend_.start_matchmaking(request);
}

Result<void> OnlineProductController::cancel_matchmaking() {
    return backend_.cancel_matchmaking();
}

Result<DirectRoomDescriptor> OnlineProductController::create_direct_room(
    const OnlineMatchRequest& request) {
    if (request.mode != OnlineMode::direct) {
        return Result<DirectRoomDescriptor>::failure(
            ErrorCode::invalid_argument,
            "only direct mode may create a direct room");
    }

    const auto validated = validate_request(request);
    if (!validated) {
        return Result<DirectRoomDescriptor>::failure(validated.error, validated.detail);
    }

    auto room = backend_.create_direct_room(request);
    if (!room) return room;

    const auto room_validated = validate_room(room.value);
    if (!room_validated) {
        return Result<DirectRoomDescriptor>::failure(room_validated.error, room_validated.detail);
    }
    return room;
}

Result<void> OnlineProductController::join_direct_room(std::string_view invite_code) {
    if (blank(invite_code)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "invite code must not be blank");
    }
    return backend_.join_direct_room(invite_code);
}

Result<void> OnlineProductController::validate_room(const DirectRoomDescriptor& room) const {
    if (blank(room.room_id)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "direct room id must not be blank");
    }
    if (blank(room.invite_code)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "direct room invite code must not be blank");
    }

    OnlineMatchRequest request{};
    request.mode = OnlineMode::direct;
    request.rules = room.rules;
    request.network = room.network;
    request.mod_policy = room.mod_policy;
    return validate_request(request);
}

OnlineConnectionLifecycle online_lifecycle_from_network_state(
    NetworkConnectionState state) noexcept {
    switch (state) {
        case NetworkConnectionState::connected:
            return OnlineConnectionLifecycle::connected;
        case NetworkConnectionState::reconnecting:
            return OnlineConnectionLifecycle::reconnecting;
        case NetworkConnectionState::disconnected:
            return OnlineConnectionLifecycle::disconnected;
    }
    return OnlineConnectionLifecycle::disconnected;
}

ConnectionQuality evaluate_connection_quality(const OnlineConnectionMetrics& metrics) noexcept {
    const auto predicted_percent = metrics.observed_frames == 0
        ? 0.0
        : 100.0 * static_cast<double>(metrics.network.predicted_frames)
            / static_cast<double>(metrics.observed_frames);
    const auto rollback_depth = std::max(
        metrics.network.last_rollback_depth,
        metrics.network.max_rollback_depth);

    const std::array<int, 5> impairment{
        rtt_impairment(metrics.network.rtt_ms),
        jitter_impairment(metrics.network.jitter_ms),
        loss_impairment(metrics.network.packet_loss_percent()),
        prediction_impairment(predicted_percent),
        rollback_impairment(rollback_depth),
    };

    const auto max_impairment = *std::max_element(impairment.begin(), impairment.end());
    int total = 0;
    for (const auto value : impairment) total += value;
    const auto average_ceiling = (total + 4) / 5;

    if (max_impairment == 0) return ConnectionQuality::excellent;
    if (max_impairment <= 1 && average_ceiling <= 1) return ConnectionQuality::good;
    if (max_impairment <= 2 && average_ceiling <= 2) return ConnectionQuality::fair;
    if (max_impairment <= 3) return ConnectionQuality::poor;
    return ConnectionQuality::unusable;
}

ConnectionIndicator make_connection_indicator(
    const OnlineConnectionMetrics& metrics,
    OnlineConnectionLifecycle lifecycle) {
    if (lifecycle == OnlineConnectionLifecycle::disconnected) {
        return {ConnectionQuality::unusable, lifecycle, 0, "signal-0", "Disconnected"};
    }
    if (lifecycle == OnlineConnectionLifecycle::left) {
        return {ConnectionQuality::unusable, lifecycle, 0, "signal-0", "Left match"};
    }

    const auto quality = evaluate_connection_quality(metrics);
    auto [bars, quality_text] = signal_for_quality(quality);
    ConnectionIndicator indicator{
        quality,
        lifecycle,
        bars,
        "signal-" + std::to_string(bars),
        std::move(quality_text),
    };
    if (lifecycle == OnlineConnectionLifecycle::reconnecting) {
        indicator.text = "Reconnecting";
    }
    return indicator;
}

Result<void> OnlineMatchHistory::append(MatchHistoryEntry entry) {
    if (capacity_ == 0) {
        return Result<void>::failure(ErrorCode::invalid_argument, "history capacity must be greater than zero");
    }
    if (blank(entry.match_id)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "match id must not be blank");
    }
    if (entry.local_rounds > 9 || entry.remote_rounds > 9) {
        return Result<void>::failure(ErrorCode::invalid_argument, "history round counts must be 0..9");
    }
    if (std::any_of(entries_.begin(), entries_.end(), [&](const MatchHistoryEntry& existing) {
            return existing.match_id == entry.match_id;
        })) {
        return Result<void>::failure(ErrorCode::invalid_argument, "match id already exists in history");
    }

    if (entries_.size() == capacity_) {
        entries_.pop_front();
    }
    entries_.push_back(std::move(entry));
    return Result<void>::success();
}

} // namespace jojo
