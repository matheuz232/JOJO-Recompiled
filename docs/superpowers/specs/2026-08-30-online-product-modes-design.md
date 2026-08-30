# M9 Online Product Modes Design

## Goal

Complete the portable product contract for the in-game Online area without coupling gameplay simulation to a public service implementation. M9 must expose Casual, Ranked, Direct 1v1 and Custom modes; deterministic rules/mod compatibility; profile/history/replay data; network settings and telemetry; and accessible connection quality indicators that distinguish reconnecting, disconnected and voluntary leave.

## Scope and boundary

M9 owns the reusable product/orchestration model. It may call service interfaces, M6 mod policy, and M8 telemetry, but it does not implement or claim a public account service, matchmaking fleet, NAT traversal/relay, encryption/key exchange, production socket threading, or commercial-game UI rendering. Those require host/backend integration outside this portable core.

No zero-latency claim is allowed. Quality reporting describes observed network behavior only.

## Existing contracts reused

- M6 `validate_mod_session()` remains the only authority for ranked/custom mod legality.
- M8 `NetworkTelemetry` remains the source for RTT, jitter, packet counts/loss, predicted frames, rollback depth and base connection state.
- M8 rollback simulation and packet transport are not duplicated in M9.

## Files

- `src/core/online.h/.cpp`: product modes, rules, mod policy mapping, connection quality/indicator, profile/history, backend interface and orchestration.
- `src/core/replay.h/.cpp`: deterministic portable replay recording and binary serialization.
- `tests/test_online.cpp`: permanent M9 behavior/integration contract.
- `CMakeLists.txt`: add M9 sources/test target.
- `docs/architecture/PRODUCTION-ROADMAP.md`: mark M9 complete only after full verification.
- `docs/superpowers/plans/2026-08-30-online-product-modes-verification.md`: evidence and readiness boundary.

## Online menu and modes

```cpp
enum class OnlineMode {
    casual,
    ranked,
    direct,
    custom,
};

struct OnlineMenuEntry {
    OnlineMode mode{};
    std::string title;
    std::string description;
};

const std::vector<OnlineMenuEntry>& default_online_menu_entries();
```

The default list is stable and ordered Casual, Ranked, Direct 1v1, Custom. Profile/history/replays and Network Settings are supporting Online surfaces rather than extra match modes.

## Match rules and network settings

```cpp
struct MatchRules {
    std::uint8_t rounds_to_win{2};
    bool timer_enabled{true};
    std::uint16_t timer_seconds{99};
};

struct OnlineNetworkSettings {
    std::uint32_t max_rollback_frames{8};
    std::uint32_t input_delay_frames{};
    bool show_telemetry{true};
};

Result<void> validate_match_rules(const MatchRules& rules);
Result<void> validate_online_network_settings(const OnlineNetworkSettings& settings);
```

Rules accept 1..5 rounds. With timer enabled, seconds must be 30..999; with timer disabled the numeric value is ignored. Network settings accept 1..20 maximum rollback frames and 0..8 local input-delay frames.

## Mod policy

```cpp
enum class OnlineModPolicyKind {
    ranked_legal_only,
    exact_mod_set,
    unrestricted,
};

struct OnlineModPolicy {
    OnlineModPolicyKind kind{OnlineModPolicyKind::exact_mod_set};
    std::string required_mod_set_hash;
};

Result<ModSessionPolicy> make_mod_session_policy(
    OnlineMode mode,
    const OnlineModPolicy& online_policy,
    const ModSetHashes& local_hashes);
```

Rules:

- Ranked always maps to `ModSessionMode::ranked`; an attempt to use another online mod-policy kind is rejected.
- Casual uses exact mod-set compatibility by default and maps to `ModSessionMode::custom` with the local full mod-set hash. This favors deterministic peer compatibility over silently allowing different data.
- Direct and Custom accept `exact_mod_set` or `unrestricted`; `ranked_legal_only` is also accepted when hosts want ranked legality without ranked matchmaking.
- `exact_mod_set` uses an explicit required hash when supplied, otherwise the local full mod-set hash.
- Every match/room validation calls existing M6 `validate_mod_session()`; M9 does not reimplement gameplay-mod inspection.

## Requests, direct rooms and service boundary

```cpp
struct OnlineMatchRequest {
    OnlineMode mode{OnlineMode::casual};
    MatchRules rules{};
    OnlineNetworkSettings network{};
    OnlineModPolicy mod_policy{};
};

struct DirectRoomDescriptor {
    std::string room_id;
    std::string invite_code;
    MatchRules rules{};
    OnlineNetworkSettings network{};
    OnlineModPolicy mod_policy{};
};

class IOnlineBackend {
public:
    virtual ~IOnlineBackend() = default;
    virtual Result<void> start_matchmaking(const OnlineMatchRequest& request) = 0;
    virtual Result<void> cancel_matchmaking() = 0;
    virtual Result<DirectRoomDescriptor> create_direct_room(const OnlineMatchRequest& request) = 0;
    virtual Result<void> join_direct_room(std::string_view invite_code) = 0;
};
```

`OnlineProductController` validates rules, network settings and M6 mod legality before forwarding a request. Casual/Ranked use `start_matchmaking`; Direct creates or joins an invite room; Custom may use `start_matchmaking` as a service-defined custom-lobby/search operation. Direct room descriptors require non-empty room and invite identifiers and valid contained configuration.

## Connection lifecycle, quality and accessible indicator

```cpp
enum class OnlineConnectionLifecycle {
    connected,
    reconnecting,
    disconnected,
    left,
};

enum class ConnectionQuality {
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
};

ConnectionQuality evaluate_connection_quality(const OnlineConnectionMetrics& metrics) noexcept;
ConnectionIndicator make_connection_indicator(
    const OnlineConnectionMetrics& metrics,
    OnlineConnectionLifecycle lifecycle);
```

Quality uses all of RTT, jitter, packet-loss percentage, prediction rate and rollback behavior. It must never be a ping-only classifier.

A deterministic impairment score is used:

- RTT: <=60/100/150/220/>220 ms => 0/1/2/3/4 points.
- Jitter: <=5/12/25/45/>45 ms => 0/1/2/3/4.
- Loss: <=0.5/1.5/3/6/>6 percent => 0/1/2/3/4.
- Predicted-frame rate: <=1/3/7/15/>15 percent => 0/1/2/3/4; zero observed frames contributes 0.
- Rollback: use the worse of last/max rollback depth, <=1/2/4/6/>6 => 0/1/2/3/4.

The quality bucket is based on the maximum impairment plus the rounded-up average impairment: excellent only when max=0; good when max<=1 and average<=1; fair when max<=2 and average<=2; poor when max<=3; otherwise unusable. This intentionally prevents a severe single metric from being hidden by good ping.

Indicators expose both non-color signal tokens (`signal-4` through `signal-0`) and text. `reconnecting` text remains distinct, `disconnected` forces unusable/0 bars, and voluntary `left` is distinct from network loss and also forces 0 bars.

## Profile and history

```cpp
enum class MatchOutcome { win, loss, draw, disconnected, left };

struct OnlineProfile {
    std::string player_id;
    std::string display_name;
    std::int32_t ranked_rating{1000};
    std::uint64_t casual_wins{};
    std::uint64_t casual_losses{};
    std::uint64_t ranked_wins{};
    std::uint64_t ranked_losses{};
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
};

class OnlineMatchHistory {
public:
    explicit OnlineMatchHistory(std::size_t capacity);
    Result<void> append(MatchHistoryEntry entry);
    const std::deque<MatchHistoryEntry>& entries() const noexcept;
};
```

History rejects zero capacity, empty match IDs, duplicate IDs and invalid round counts above 9. It is bounded and evicts the oldest entry when full.

## Replay contract

```cpp
inline constexpr std::uint16_t kReplayFormatVersion = 1;

struct ReplayFrame {
    std::uint64_t frame{};
    RollbackInput local{};
    RollbackInput remote{};
    std::string state_hash_hex;
};

struct OnlineReplay {
    std::string replay_id;
    OnlineMode mode{OnlineMode::casual};
    std::uint64_t rng_seed{};
    std::string initial_state_hash_hex;
    std::string mod_set_hash;
    std::vector<ReplayFrame> frames;
};

Result<void> validate_online_replay(const OnlineReplay& replay);
Result<std::vector<std::uint8_t>> serialize_online_replay(const OnlineReplay& replay);
Result<OnlineReplay> parse_online_replay(std::span<const std::uint8_t> bytes);
```

Replay bytes are deterministic little-endian and contain only portable metadata, inputs and hashes. They contain no copyrighted game assets or raw commercial snapshots. Replay IDs must be non-empty. Initial/frame state hashes must be 64 hexadecimal characters. Frame indexes are strictly increasing. Strings are length-prefixed with bounded lengths and the parser rejects bad magic/version/mode, truncation, trailing bytes and excessive counts/lengths.

## Test requirements

Permanent M9 tests must prove:

1. stable four-mode Online menu and supporting product surfaces;
2. rule/network-setting validation boundaries;
3. Ranked delegates to M6 ranked policy and rejects gameplay mods;
4. Casual exact compatibility and Direct/Custom exact/unrestricted/ranked-legal policy mapping;
5. controller validates before backend calls and direct room/invite validation;
6. quality classification reacts to RTT, jitter, loss, prediction and rollback independently;
7. signal token + text and all connected/reconnecting/disconnected/left lifecycle distinctions;
8. bounded profile/history behavior and duplicate rejection;
9. deterministic replay serialization round-trip and stable bytes;
10. replay malformed/version/hash/order/truncation/trailing-data rejection;
11. Linux and Windows/MSVC full build + CTest, including Windows executable artifact.

## Completion gate

M9 is complete only when the permanent implementation and tests are merged, final branch CI is green on the exact PR head, the PR is merged with expected-head protection, and a new `main` workflow passes Linux and Windows/MSVC with a Windows executable artifact.