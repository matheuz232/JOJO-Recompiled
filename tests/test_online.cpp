#include "core/online.h"
#include "core/replay.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; \
        ++failures; \
    } \
} while (false)

class FakeOnlineBackend final : public jojo::IOnlineBackend {
public:
    jojo::Result<void> start_matchmaking(const jojo::OnlineMatchRequest& request) override {
        ++start_calls;
        last_request = request;
        return start_result
            ? jojo::Result<void>::success()
            : jojo::Result<void>::failure(jojo::ErrorCode::invalid_argument, "backend rejected matchmaking");
    }

    jojo::Result<void> cancel_matchmaking() override {
        ++cancel_calls;
        return jojo::Result<void>::success();
    }

    jojo::Result<jojo::DirectRoomDescriptor> create_direct_room(
        const jojo::OnlineMatchRequest& request) override {
        ++create_calls;
        last_request = request;
        jojo::DirectRoomDescriptor room{};
        room.room_id = invalid_room ? "" : "room-123";
        room.invite_code = "INVITE-ABC";
        room.rules = request.rules;
        room.network = request.network;
        room.mod_policy = request.mod_policy;
        return jojo::Result<jojo::DirectRoomDescriptor>::success(std::move(room));
    }

    jojo::Result<void> join_direct_room(std::string_view invite_code) override {
        ++join_calls;
        last_invite = std::string(invite_code);
        return jojo::Result<void>::success();
    }

    int start_calls{};
    int cancel_calls{};
    int create_calls{};
    int join_calls{};
    bool start_result{true};
    bool invalid_room{};
    jojo::OnlineMatchRequest last_request{};
    std::string last_invite;
};

jojo::DiscoveredMod make_mod(std::string id, bool gameplay) {
    jojo::DiscoveredMod mod{};
    mod.manifest.id = std::move(id);
    mod.manifest.name = mod.manifest.id;
    mod.manifest.version = {1, 0, 0};
    mod.manifest.api_version = jojo::kModApiVersion;
    mod.manifest.gameplay = gameplay;
    return mod;
}

jojo::OnlineConnectionMetrics excellent_metrics() {
    jojo::OnlineConnectionMetrics metrics{};
    metrics.network.rtt_ms = 40.0;
    metrics.network.jitter_ms = 3.0;
    metrics.network.packets_received = 100;
    metrics.network.packets_lost = 0;
    metrics.network.predicted_frames = 0;
    metrics.network.last_rollback_depth = 1;
    metrics.network.max_rollback_depth = 1;
    metrics.observed_frames = 600;
    return metrics;
}

jojo::OnlineReplay sample_replay() {
    jojo::OnlineReplay replay{};
    replay.replay_id = "replay-001";
    replay.mode = jojo::OnlineMode::direct;
    replay.rng_seed = 0x1122334455667788ull;
    replay.initial_state_hash_hex = std::string(64, 'a');
    replay.mod_set_hash = "mods-hash-001";
    replay.frames = {
        jojo::ReplayFrame{10, {0x11u, -120, 300}, {0x22u, 45, -99}, std::string(64, 'b')},
        jojo::ReplayFrame{11, {0x33u, 0, -32768}, {0x44u, 32767, 1}, std::string(64, 'c')},
        jojo::ReplayFrame{15, {0x55u, 5, 6}, {0x66u, 7, 8}, std::string(64, 'd')},
    };
    return replay;
}

void test_menu_and_validation() {
    const auto& menu = jojo::default_online_menu_entries();
    CHECK(menu.size() == 4);
    CHECK(menu[0].mode == jojo::OnlineMode::casual);
    CHECK(menu[1].mode == jojo::OnlineMode::ranked);
    CHECK(menu[2].mode == jojo::OnlineMode::direct);
    CHECK(menu[3].mode == jojo::OnlineMode::custom);
    for (const auto& entry : menu) {
        CHECK(!entry.title.empty());
        CHECK(!entry.description.empty());
    }

    CHECK(jojo::validate_match_rules({1, true, 30}));
    CHECK(jojo::validate_match_rules({5, true, 999}));
    CHECK(jojo::validate_match_rules({2, false, 0}));
    CHECK(!jojo::validate_match_rules({0, true, 99}));
    CHECK(!jojo::validate_match_rules({6, true, 99}));
    CHECK(!jojo::validate_match_rules({2, true, 29}));
    CHECK(!jojo::validate_match_rules({2, true, 1000}));

    CHECK(jojo::validate_online_network_settings({1, 0, true}));
    CHECK(jojo::validate_online_network_settings({20, 8, false}));
    CHECK(!jojo::validate_online_network_settings({0, 0, true}));
    CHECK(!jojo::validate_online_network_settings({21, 0, true}));
    CHECK(!jojo::validate_online_network_settings({8, 9, true}));
}

void test_mod_policy_and_controller() {
    auto cosmetic = make_mod("cosmetic", false);
    auto gameplay = make_mod("gameplay", true);
    jojo::ResolvedModSet cosmetic_set{{&cosmetic}, {}};
    jojo::ResolvedModSet gameplay_set{{&gameplay}, {}};
    jojo::ModSetHashes hashes{"full-mod-set-hash", "gameplay-hash"};

    jojo::OnlineModPolicy exact{};
    exact.kind = jojo::OnlineModPolicyKind::exact_mod_set;

    auto casual = jojo::make_mod_session_policy(jojo::OnlineMode::casual, exact, hashes);
    CHECK(casual);
    CHECK(casual.value.mode == jojo::ModSessionMode::custom);
    CHECK(casual.value.required_mod_set_hash == hashes.mod_set_hash);

    auto unrestricted = exact;
    unrestricted.kind = jojo::OnlineModPolicyKind::unrestricted;
    CHECK(!jojo::make_mod_session_policy(jojo::OnlineMode::casual, unrestricted, hashes));

    jojo::OnlineModPolicy ranked_policy{};
    ranked_policy.kind = jojo::OnlineModPolicyKind::ranked_legal_only;
    auto ranked = jojo::make_mod_session_policy(jojo::OnlineMode::ranked, ranked_policy, hashes);
    CHECK(ranked);
    CHECK(ranked.value.mode == jojo::ModSessionMode::ranked);
    CHECK(!jojo::make_mod_session_policy(jojo::OnlineMode::ranked, exact, hashes));

    auto direct_unrestricted = jojo::make_mod_session_policy(
        jojo::OnlineMode::direct, unrestricted, hashes);
    CHECK(direct_unrestricted);
    CHECK(direct_unrestricted.value.mode == jojo::ModSessionMode::custom);
    CHECK(direct_unrestricted.value.required_mod_set_hash.empty());

    auto direct_ranked = jojo::make_mod_session_policy(
        jojo::OnlineMode::direct, ranked_policy, hashes);
    CHECK(direct_ranked);
    CHECK(direct_ranked.value.mode == jojo::ModSessionMode::ranked);

    auto explicit_exact = exact;
    explicit_exact.required_mod_set_hash = "remote-required-hash";
    auto custom_exact = jojo::make_mod_session_policy(
        jojo::OnlineMode::custom, explicit_exact, hashes);
    CHECK(custom_exact);
    CHECK(custom_exact.value.required_mod_set_hash == "remote-required-hash");

    FakeOnlineBackend backend;
    jojo::OnlineProductController cosmetic_controller(backend, cosmetic_set, hashes);

    jojo::OnlineMatchRequest casual_request{};
    casual_request.mode = jojo::OnlineMode::casual;
    casual_request.mod_policy = exact;
    CHECK(cosmetic_controller.begin_matchmaking(casual_request));
    CHECK(backend.start_calls == 1);
    CHECK(backend.last_request.mode == jojo::OnlineMode::casual);

    jojo::OnlineMatchRequest custom_request{};
    custom_request.mode = jojo::OnlineMode::custom;
    custom_request.mod_policy = unrestricted;
    CHECK(cosmetic_controller.begin_matchmaking(custom_request));
    CHECK(backend.start_calls == 2);

    jojo::OnlineMatchRequest direct_request{};
    direct_request.mode = jojo::OnlineMode::direct;
    direct_request.mod_policy = exact;
    CHECK(!cosmetic_controller.begin_matchmaking(direct_request));
    CHECK(backend.start_calls == 2);

    auto room = cosmetic_controller.create_direct_room(direct_request);
    CHECK(room);
    CHECK(backend.create_calls == 1);
    CHECK(room.value.room_id == "room-123");
    CHECK(room.value.invite_code == "INVITE-ABC");

    backend.invalid_room = true;
    CHECK(!cosmetic_controller.create_direct_room(direct_request));
    CHECK(backend.create_calls == 2);
    backend.invalid_room = false;

    CHECK(!cosmetic_controller.join_direct_room("   \t"));
    CHECK(backend.join_calls == 0);
    CHECK(cosmetic_controller.join_direct_room("JOIN-ME"));
    CHECK(backend.join_calls == 1);
    CHECK(backend.last_invite == "JOIN-ME");

    jojo::OnlineProductController gameplay_controller(backend, gameplay_set, hashes);
    jojo::OnlineMatchRequest ranked_request{};
    ranked_request.mode = jojo::OnlineMode::ranked;
    ranked_request.mod_policy = ranked_policy;
    const auto starts_before_ranked = backend.start_calls;
    CHECK(!gameplay_controller.begin_matchmaking(ranked_request));
    CHECK(backend.start_calls == starts_before_ranked);

    CHECK(cosmetic_controller.begin_matchmaking(ranked_request));
    CHECK(backend.start_calls == starts_before_ranked + 1);

    auto mismatched = direct_request;
    mismatched.mod_policy.required_mod_set_hash = "different-hash";
    const auto creates_before_mismatch = backend.create_calls;
    CHECK(!cosmetic_controller.create_direct_room(mismatched));
    CHECK(backend.create_calls == creates_before_mismatch);

    auto bad_rules = casual_request;
    bad_rules.rules.rounds_to_win = 0;
    const auto starts_before_bad_rules = backend.start_calls;
    CHECK(!cosmetic_controller.begin_matchmaking(bad_rules));
    CHECK(backend.start_calls == starts_before_bad_rules);
}

void test_connection_quality_and_lifecycle() {
    auto metrics = excellent_metrics();
    CHECK(jojo::evaluate_connection_quality(metrics) == jojo::ConnectionQuality::excellent);

    auto good = metrics;
    good.network.rtt_ms = 90.0;
    good.network.jitter_ms = 10.0;
    good.network.packets_received = 199;
    good.network.packets_lost = 1;
    good.network.predicted_frames = 12;
    good.network.last_rollback_depth = 2;
    good.network.max_rollback_depth = 2;
    good.observed_frames = 600;
    CHECK(jojo::evaluate_connection_quality(good) == jojo::ConnectionQuality::good);

    auto fair = metrics;
    fair.network.rtt_ms = 140.0;
    fair.network.jitter_ms = 20.0;
    fair.network.packets_received = 98;
    fair.network.packets_lost = 2;
    fair.network.predicted_frames = 30;
    fair.network.last_rollback_depth = 4;
    fair.network.max_rollback_depth = 4;
    fair.observed_frames = 600;
    CHECK(jojo::evaluate_connection_quality(fair) == jojo::ConnectionQuality::fair);

    auto rtt_poor = metrics;
    rtt_poor.network.rtt_ms = 180.0;
    CHECK(jojo::evaluate_connection_quality(rtt_poor) == jojo::ConnectionQuality::poor);

    auto jitter_poor = metrics;
    jitter_poor.network.jitter_ms = 30.0;
    CHECK(jojo::evaluate_connection_quality(jitter_poor) == jojo::ConnectionQuality::poor);

    auto loss_poor = metrics;
    loss_poor.network.packets_received = 96;
    loss_poor.network.packets_lost = 4;
    CHECK(jojo::evaluate_connection_quality(loss_poor) == jojo::ConnectionQuality::poor);

    auto prediction_poor = metrics;
    prediction_poor.network.predicted_frames = 60;
    prediction_poor.observed_frames = 600;
    CHECK(jojo::evaluate_connection_quality(prediction_poor) == jojo::ConnectionQuality::poor);

    auto rollback_poor = metrics;
    rollback_poor.network.max_rollback_depth = 5;
    CHECK(jojo::evaluate_connection_quality(rollback_poor) == jojo::ConnectionQuality::poor);

    auto unusable = metrics;
    unusable.network.rtt_ms = 300.0;
    CHECK(jojo::evaluate_connection_quality(unusable) == jojo::ConnectionQuality::unusable);

    auto connected = jojo::make_connection_indicator(metrics, jojo::OnlineConnectionLifecycle::connected);
    CHECK(connected.quality == jojo::ConnectionQuality::excellent);
    CHECK(connected.signal_bars == 4);
    CHECK(connected.signal_token == "signal-4");
    CHECK(connected.text == "Excellent");

    auto reconnecting = jojo::make_connection_indicator(good, jojo::OnlineConnectionLifecycle::reconnecting);
    CHECK(reconnecting.quality == jojo::ConnectionQuality::good);
    CHECK(reconnecting.signal_bars == 3);
    CHECK(reconnecting.text == "Reconnecting");

    auto disconnected = jojo::make_connection_indicator(metrics, jojo::OnlineConnectionLifecycle::disconnected);
    CHECK(disconnected.quality == jojo::ConnectionQuality::unusable);
    CHECK(disconnected.signal_bars == 0);
    CHECK(disconnected.signal_token == "signal-0");
    CHECK(disconnected.text == "Disconnected");

    auto left = jojo::make_connection_indicator(metrics, jojo::OnlineConnectionLifecycle::left);
    CHECK(left.quality == jojo::ConnectionQuality::unusable);
    CHECK(left.signal_bars == 0);
    CHECK(left.text == "Left match");

    CHECK(jojo::online_lifecycle_from_network_state(jojo::NetworkConnectionState::connected)
        == jojo::OnlineConnectionLifecycle::connected);
    CHECK(jojo::online_lifecycle_from_network_state(jojo::NetworkConnectionState::reconnecting)
        == jojo::OnlineConnectionLifecycle::reconnecting);
    CHECK(jojo::online_lifecycle_from_network_state(jojo::NetworkConnectionState::disconnected)
        == jojo::OnlineConnectionLifecycle::disconnected);
}

void test_profile_and_history() {
    jojo::OnlineProfile profile{};
    profile.player_id = "player-1";
    profile.display_name = "Jotaro";
    profile.ranked_rating = 1200;
    profile.casual_wins = 3;
    profile.ranked_wins = 2;
    CHECK(profile.player_id == "player-1");
    CHECK(profile.ranked_rating == 1200);

    jojo::OnlineMatchHistory zero(0);
    jojo::MatchHistoryEntry first{};
    first.match_id = "m1";
    first.opponent_name = "DIO";
    first.outcome = jojo::MatchOutcome::win;
    first.quality = jojo::ConnectionQuality::good;
    CHECK(!zero.append(first));

    jojo::OnlineMatchHistory history(2);
    CHECK(history.append(first));
    CHECK(!history.append(first));

    auto second = first;
    second.match_id = "m2";
    second.mode = jojo::OnlineMode::ranked;
    second.outcome = jojo::MatchOutcome::loss;
    second.replay_id = "r2";
    CHECK(history.append(second));
    CHECK(history.entries().size() == 2);

    auto third = first;
    third.match_id = "m3";
    third.mode = jojo::OnlineMode::direct;
    third.outcome = jojo::MatchOutcome::left;
    CHECK(history.append(third));
    CHECK(history.entries().size() == 2);
    CHECK(history.entries().front().match_id == "m2");
    CHECK(history.entries().back().match_id == "m3");

    auto bad = first;
    bad.match_id = "bad-rounds";
    bad.local_rounds = 10;
    CHECK(!history.append(bad));

    bad = first;
    bad.match_id.clear();
    CHECK(!history.append(bad));
}

void test_replay_roundtrip_and_rejection() {
    const auto replay = sample_replay();
    CHECK(jojo::validate_online_replay(replay));

    auto bytes1 = jojo::serialize_online_replay(replay);
    auto bytes2 = jojo::serialize_online_replay(replay);
    CHECK(bytes1);
    CHECK(bytes2);
    CHECK(bytes1.value == bytes2.value);

    auto parsed = jojo::parse_online_replay(bytes1.value);
    CHECK(parsed);
    CHECK(parsed.value == replay);

    auto invalid_hash = replay;
    invalid_hash.initial_state_hash_hex = "1234";
    CHECK(!jojo::validate_online_replay(invalid_hash));
    CHECK(!jojo::serialize_online_replay(invalid_hash));

    auto invalid_frame_hash = replay;
    invalid_frame_hash.frames[1].state_hash_hex[0] = 'z';
    CHECK(!jojo::validate_online_replay(invalid_frame_hash));

    auto out_of_order = replay;
    out_of_order.frames[2].frame = 11;
    CHECK(!jojo::validate_online_replay(out_of_order));

    auto empty_id = replay;
    empty_id.replay_id.clear();
    CHECK(!jojo::validate_online_replay(empty_id));

    auto bad_magic = bytes1.value;
    bad_magic[0] = 'X';
    CHECK(!jojo::parse_online_replay(bad_magic));

    auto bad_version = bytes1.value;
    bad_version[4] = 2;
    bad_version[5] = 0;
    CHECK(!jojo::parse_online_replay(bad_version));

    auto bad_mode = bytes1.value;
    bad_mode[6] = 255;
    CHECK(!jojo::parse_online_replay(bad_mode));

    auto bad_reserved = bytes1.value;
    bad_reserved[7] = 1;
    CHECK(!jojo::parse_online_replay(bad_reserved));

    auto truncated = bytes1.value;
    truncated.pop_back();
    CHECK(!jojo::parse_online_replay(truncated));

    auto trailing = bytes1.value;
    trailing.push_back(0);
    CHECK(!jojo::parse_online_replay(trailing));
}

} // namespace

int main() {
    test_menu_and_validation();
    test_mod_policy_and_controller();
    test_connection_quality_and_lifecycle();
    test_profile_and_history();
    test_replay_roundtrip_and_rejection();

    if (failures != 0) {
        std::cerr << failures << " online-product assertion(s) failed\n";
        return 1;
    }
    return 0;
}
