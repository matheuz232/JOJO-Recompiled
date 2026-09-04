# R2.5 Online Product Flow Design

## Status

Approved design for the next R2.5 increment after the integrated `OnlineSessionController` checkpoint.

This document defines a portable product-facing Direct Online flow. It does not implement a rendered commercial-game menu, matchmaking, ranked services, public rooms, invitations, accounts, NAT traversal, relay infrastructure, DNS/hostnames, encryption/authentication, spectator mode, replays, or commercial game-specific online integration.

## Context

The repository already contains a tested and integrated `OnlineSessionController` in `jojo_core`. That controller owns the direct UDP session lifecycle, endpoint pinning, reconnect/liveness behavior, telemetry, gameplay gating, explicit disconnect/reset, and fault semantics.

The current Windows executable shell in `src/app_win32/main.cpp` is the first-run game-image preparation/conversion UI. Product architecture explicitly states that graphics, controls, training, mods, and online options belong to the in-game UI; the first-run shell is not a launcher/settings application.

There is not yet a rendered in-game product shell where Online can be attached directly. Therefore this increment must introduce a portable Online product-flow model that can later be rendered by the real in-game UI without contaminating the conversion shell or duplicating network/session logic.

## Goal

Add a portable `OnlineMenuSession` model to `jojo_core` that:

- exposes only Direct Host/Join capabilities that genuinely exist today;
- wraps one `OnlineSessionController` rather than reimplementing transport/session behavior;
- presents stable product/UI states and actions;
- validates join endpoint text before starting a session;
- exposes local/remote endpoints, role, telemetry, and actionable error state;
- exposes whether gameplay may begin, derived strictly from the controller gameplay gate;
- preserves gameplay packets returned by the controller instead of consuming them inside the UI model;
- supports clean navigation back to Home without silently disconnecting a connected peer;
- remains portable and testable on Linux and Windows/MSVC.

## Non-goals

This increment does not add or claim:

- Casual matchmaking;
- Competitive/ranked matchmaking;
- matchmaking queues or rating services;
- public room discovery;
- room invitations;
- account/profile/history services;
- relay service or NAT traversal;
- STUN/TURN/ICE-like infrastructure;
- hostname/DNS endpoint input;
- IPv6 endpoint input;
- encryption, authentication, identity verification, or anti-cheat;
- spectator mode;
- replay services;
- mod-set negotiation beyond already-existing lower-level contracts;
- a Win32 Online screen inside the first-run conversion shell;
- a rendered commercial-game Online menu;
- a commercial-game rollback adapter or proof of commercial online compatibility;
- promotion of R2.5 to `verified`.

## Chosen Architecture

Create a portable product-flow model in `jojo_core`:

- `src/core/online_menu.h`
- `src/core/online_menu.cpp`
- `tests/test_online_menu.cpp`

`OnlineMenuSession` owns exactly one `OnlineSessionController` and translates controller lifecycle state into product-facing menu state.

The component is an application/UI model, not a transport layer. It must not open sockets directly, retransmit packets directly, calculate RTT/jitter/loss independently, maintain a second reconnect timer, or invent a second peer lifecycle.

The existing `OnlineSessionController` remains the sole owner of Direct session behavior.

## Product State Model

### `OnlineMenuScreen`

```cpp
enum class OnlineMenuScreen {
    home,
    hosting,
    joining,
    connected,
    reconnecting,
    disconnected,
    faulted,
};
```

State mapping is intentionally simple:

- `OnlineSessionState::inactive` -> `home` when no start action is active;
- `OnlineSessionState::waiting_for_peer` -> `hosting`;
- `OnlineSessionState::connecting` -> `joining`;
- `OnlineSessionState::connected` -> `connected`;
- `OnlineSessionState::reconnecting` -> `reconnecting`;
- `OnlineSessionState::disconnected` -> `disconnected`;
- `OnlineSessionState::faulted` -> `faulted`.

The menu must not invent additional network states such as “searching,” “match found,” “room ready,” or “ranked connection.”

## View State

The portable UI snapshot is:

```cpp
struct OnlineMenuViewState {
    OnlineMenuScreen screen{OnlineMenuScreen::home};

    std::string join_endpoint_text{};
    std::string validation_error{};

    std::optional<DirectSessionRole> role{};
    std::optional<NetworkEndpoint> local_endpoint{};
    std::optional<NetworkEndpoint> remote_endpoint{};

    double rtt_ms{};
    double jitter_ms{};
    double packet_loss_percent{};
    std::uint64_t packets_sent{};
    std::uint64_t packets_received{};
    std::uint64_t packets_lost{};

    bool can_host{true};
    bool can_join{true};
    bool can_disconnect{};
    bool can_return_home{};
    bool can_start_gameplay{};

    ErrorCode session_error{ErrorCode::none};
    std::string session_error_detail{};
};
```

### Field semantics

- `join_endpoint_text` is UI-owned text and is not stored inside the transport/controller.
- `validation_error` is only for local form/input validation and must not be confused with an operational network/session fault.
- `role`, endpoints, telemetry, and session errors mirror `OnlineSessionController::view()`.
- `can_start_gameplay` is always derived from `OnlineSessionController::view().can_send_gameplay`.
- `can_disconnect` is true only when the underlying controller is connected.
- `can_return_home` is true in Hosting, Joining, Reconnecting, Disconnected, and Faulted; it is false in Home and Connected. Connected must use explicit `disconnect(now_ms)` before returning Home.
- `can_host` and `can_join` are true only in Home when no active controller session exists.

## Public API

The intended API is:

```cpp
class OnlineMenuSession {
public:
    [[nodiscard]] const OnlineMenuViewState& view() const noexcept;

    void set_join_endpoint(std::string text);

    [[nodiscard]] Result<void> start_host(
        NetworkEndpoint local,
        DirectSessionTiming timing = {});

    [[nodiscard]] Result<void> start_join(
        NetworkEndpoint local,
        DirectSessionTiming timing,
        std::uint64_t now_ms);

    [[nodiscard]] Result<std::vector<NetworkPacket>> tick(
        std::uint64_t now_ms);

    [[nodiscard]] Result<void> disconnect(
        std::uint64_t now_ms);

    void return_home() noexcept;

private:
    void refresh_view();

    OnlineSessionController controller_{};
    OnlineMenuViewState view_{};
};
```

Exact private helpers may differ during implementation, but the public semantics in this specification are normative.

## Actions and Data Flow

### Editing join endpoint

`set_join_endpoint(text)` only updates `join_endpoint_text` and clears any prior form-level `validation_error`.

It must not parse immediately, open sockets, alter session state, or produce a controller fault. It may update the retained text while another screen is active, but this never changes the active network session and does not make Join available outside Home.

### Starting Host

`start_host(local, timing)` is valid only from Home.

It delegates to `OnlineSessionController::host(local, timing)`.

On success:

- screen becomes `hosting` through controller-state mapping;
- resolved local endpoint is exposed, including the actual ephemeral port when local port zero was requested;
- role is Host;
- gameplay remains blocked.

On operational controller failure:

- screen becomes `faulted`;
- controller `ErrorCode` and detail are exposed;
- retry requires reset via `return_home()`.

Calling `start_host` outside Home returns `invalid_argument`, preserves the current lifecycle state, and does not create or overwrite a session fault.

### Starting Join

`start_join(local, timing, now_ms)` is valid only from Home.

It first parses `join_endpoint_text` with the already-existing `parse_direct_endpoint` contract.

If parsing fails:

- remain on Home;
- populate `validation_error` from the parser detail;
- do not call `OnlineSessionController::join`;
- do not set controller/session fault state;
- allow the user to correct the field and retry immediately.

If parsing succeeds, delegate to `OnlineSessionController::join(local, remote, timing, now_ms)`.

On success:

- screen becomes `joining` while the controller is connecting;
- remote endpoint is exposed immediately;
- gameplay remains blocked.

Operational failure follows the same `faulted` behavior as Host.

Calling `start_join` outside Home returns `invalid_argument`, preserves the current lifecycle state, and does not create or overwrite a session fault.

### Tick

`tick(now_ms)` is the single caller-driven progression point for an active product session.

It is valid only while the menu is Hosting, Joining, Connected, or Reconnecting. In Home, Disconnected, or Faulted it returns `invalid_argument` without changing the view or fault state.

For a valid active session, it delegates to `OnlineSessionController::poll(now_ms)`, refreshes the UI snapshot, and returns every gameplay packet produced by the controller.

The menu layer must not drop, rewrite, reorder, deserialize into gameplay objects, or otherwise consume returned gameplay packets. This preserves a clean handoff point for the future rollback/gameplay bridge.

The menu itself never reads wall-clock time. The caller supplies `now_ms` exactly as with the controller.

### Connected

When the controller reaches Connected:

- screen becomes `connected`;
- role and both endpoints are visible when available;
- telemetry mirrors the controller;
- `can_start_gameplay` becomes true only because the controller gameplay gate is true;
- `can_disconnect` becomes true;
- `can_return_home` remains false.

### Reconnecting

When the controller enters Reconnecting:

- screen becomes `reconnecting`;
- pinned peer endpoint remains visible;
- telemetry remains visible;
- gameplay gate is closed;
- `can_start_gameplay` is false;
- the menu does not create its own retry/reconnect timer.

If the underlying controller recovers, the menu returns to Connected on the next successful `tick()`.

If reconnect deadline expires, the menu maps to Disconnected, not Faulted.

### Disconnect

`disconnect(now_ms)` is valid only while Connected and delegates to the controller.

On success:

- the local menu immediately maps to Disconnected;
- gameplay gate closes;
- the peer will observe normal remote disconnect through its own poll/tick path.

Calling `disconnect` outside Connected returns `invalid_argument`, preserves the current lifecycle state, and does not fabricate a controller fault.

### Return Home / Reset

`return_home()` is local-only navigation/reset behavior.

Rules:

- from Home: no-op;
- from Disconnected: reset controller and clear transient UI/session state;
- from Faulted: reset controller and clear fault state;
- from Hosting, Joining, or Reconnecting: cancel locally by resetting the controller; do not fabricate a remote notification;
- from Connected: no-op; do not silently reset and do not return Home. The UI/caller must invoke `disconnect(now_ms)` first.

After a successful return to Home:

- role/endpoints/telemetry/session error are cleared;
- gameplay is blocked;
- Host/Join actions are enabled;
- the endpoint text remains available for user convenience;
- `validation_error` is cleared.

The retained endpoint-text rule prevents needless retyping while keeping transport state fully reset.

## Error Model

There are two intentionally separate classes of errors.

### Form/validation errors

Examples:

- malformed `A.B.C.D:PORT`;
- port zero in the join text;
- unsupported hostname or IPv6 syntax.

Behavior:

- reported through `validation_error`;
- remain on Home;
- controller remains inactive and non-faulted;
- user may edit and retry without reset.

### Operational session errors

Examples are errors already surfaced by `OnlineSessionController` while binding/connecting/polling/sending/disconnecting.

Behavior:

- controller transitions to or remains `faulted` according to its existing contract;
- menu maps to Faulted;
- `session_error` and `session_error_detail` preserve controller evidence;
- starting another session is forbidden until `return_home()` resets the controller.

Reconnect timeout and normal peer disconnect are lifecycle outcomes, not faults.

## Gameplay Boundary

This increment does not bridge the commercial game simulation to rollback/network packets.

It only exposes the product-level readiness gate and preserves received gameplay packets for a future caller.

Normative rules:

- `can_start_gameplay` is true only while the controller is Connected;
- Reconnecting, Disconnected, Faulted, Hosting, Joining, and Home all block gameplay start/send through the product flow;
- no commercial-game state is serialized or restored by `OnlineMenuSession`;
- no claim of commercial-game deterministic online play is permitted from these tests.

## First-run Shell Boundary

`src/app_win32/main.cpp` remains a game-image preparation/conversion shell.

This increment must not add Direct Host/Join controls, Online tabs, status panels, telemetry, or fake in-game navigation to that shell.

The product-flow model is deliberately portable so a future real in-game UI can consume it once the game/runtime presentation path can render product screens honestly.

## Testing Strategy

Add `tests/test_online_menu.cpp` and register it with CTest.

Testing must use TDD RED -> GREEN and real loopback `OnlineSessionController` behavior where practical.

The required contract includes at least:

1. initial Home state and action permissions;
2. exact `can_return_home` policy across all menu screens;
3. endpoint editing and clearing prior validation error;
4. malformed endpoint remains Home and does not fault controller behavior;
5. Host starts and maps to Hosting/Waiting for Peer;
6. Host/Join misuse outside Home returns validation failure without changing lifecycle/fault state;
7. Join starts and maps to Joining/Connecting;
8. real loopback Host/Join pair reaches Connected through caller-driven ticks;
9. local and remote endpoints are exposed correctly;
10. role is exposed correctly;
11. telemetry counters/RTT/jitter/loss mirror controller updates;
12. gameplay gate is false before connection and true only while Connected;
13. gameplay packet returned by controller poll is preserved and returned by `tick()`;
14. `tick()` in Home/Disconnected/Faulted returns validation failure without state mutation;
15. silence maps to Reconnecting without a second menu-owned timer;
16. pinned-peer reconnect recovery maps back to Connected;
17. reconnect timeout maps to Disconnected and not Faulted;
18. explicit disconnect maps local and peer views to Disconnected;
19. disconnect misuse outside Connected preserves lifecycle/fault state;
20. deterministic operational failure maps to Faulted and preserves error code/detail;
21. Faulted session cannot retry directly;
22. `return_home()` from Faulted clears the controller fault and enables a fresh Host/Join;
23. `return_home()` from Disconnected returns Home;
24. `return_home()` from Hosting/Joining/Reconnecting cancels locally and returns Home;
25. `return_home()` from Connected is a no-op and does not silently drop the peer;
26. endpoint text is retained across `return_home()` while transient errors/session evidence are cleared;
27. all prior `jojo_online_session_tests` remain green;
28. full Linux and Windows/MSVC CI, readiness gate, CTest suite, R2.5 direct UDP contract, and Windows single-executable artifact remain green.

Tests prove the portable product-flow contract only. They do not establish commercial-game compatibility or complete M9.

## Build and Repository Integration

Implementation is expected to:

- add `src/core/online_menu.cpp` to `jojo_core`;
- add `jojo_online_menu_tests` linked to `jojo_core`;
- register the test with CTest;
- leave the existing Windows executable target and conversion-shell source behavior unchanged for this increment.

No proprietary game data, extracted assets, fingerprints copied from commercial media, saves, or generated commercial content may be committed.

## Readiness and Truth Boundary

Completion of this increment does not change the canonical R2.5 status from `implemented-unverified`.

Generic CI evidence may update the R2.5 evidence reference after successful integration, but must not promote the workstream to `verified` or claim complete M9.

R2.2 and R2.4 remain blocked by legally supplied supported commercial evidence. R2.6 remains separate production validation/release work.

## Follow-up After This Increment

The next honest product step is to render/consume this portable model in the real in-game presentation path once that path exists, then bridge a Connected Direct session to the game-specific rollback/simulation adapter when commercial game-specific integration evidence is available.

Service-backed features such as matchmaking, ranked, rooms/discovery, invitations, accounts, relay/NAT traversal, profiles/history, and replay services require separately scoped infrastructure and are not implied by this design.
