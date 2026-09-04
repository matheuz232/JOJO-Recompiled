# R2.5 Online Product Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the portable Direct Online product-flow model around the integrated `OnlineSessionController`, exposing Host/Join, lifecycle, validation, telemetry, disconnect/reset behavior, packet preservation, and gameplay readiness.

**Architecture:** Add `OnlineMenuSession` to `jojo_core` as a UI/product-state model that owns exactly one `OnlineSessionController`. It maps controller lifecycle into menu state, keeps local form validation separate from operational session faults, and returns gameplay packets unchanged from `tick()`. The first-run Win32 conversion shell remains untouched.

**Tech Stack:** C++20, CMake, CTest, existing Direct UDP/`OnlineSessionController`, Linux CI, Windows x64/MSVC CI.

**Spec:** `docs/superpowers/specs/2026-09-04-r2-5-online-product-flow-design.md`

## Global Constraints

- Direct Host/Join only; no Casual, Ranked, matchmaking, public rooms, invites, accounts, relay, NAT traversal, DNS/hostnames, IPv6, encryption, spectator, replay, or service-backed UI.
- `OnlineMenuSession` must not open sockets directly or duplicate reconnect/liveness/telemetry logic.
- `OnlineSessionController` remains the sole owner of Direct session behavior.
- `src/app_win32/main.cpp` remains the first-run conversion/preparation shell; no Online controls there.
- `can_start_gameplay` is always derived from `controller_.view().can_send_gameplay`.
- `tick(now_ms)` returns controller gameplay packets unchanged and in order.
- Form validation errors never fault the controller.
- Operational session failures mirror controller fault semantics.
- Reconnect timeout and normal peer disconnect map to `disconnected`, not `faulted`.
- `return_home()` must never silently drop a connected peer.
- The caller supplies `now_ms`; the menu never reads wall-clock time.
- No proprietary commercial game data/assets/fingerprints/saves/generated content may be committed.
- Generic CI does not promote R2.5 beyond `implemented-unverified` or prove commercial online compatibility.

---

### Task 1: RED contract for the portable Online menu

**Files:**
- Create: `tests/test_online_menu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing online/network/result types.
- Produces requirements for `OnlineMenuScreen`, `OnlineMenuViewState`, and `OnlineMenuSession`.

- [ ] **Step 1: Write the failing initial-state test**

Create `tests/test_online_menu.cpp` with the same lightweight `expect()` harness style as `tests/test_online_session.cpp` and include `core/online_menu.h`.

Require:

```cpp
jojo::OnlineMenuSession menu;
const auto& view = menu.view();
expect(view.screen == jojo::OnlineMenuScreen::home, "initial screen is home");
expect(view.can_host, "home allows host");
expect(view.can_join, "home allows join");
expect(!view.can_disconnect, "home cannot disconnect");
expect(!view.can_return_home, "home has no return-home transition");
expect(!view.can_start_gameplay, "home blocks gameplay");
expect(view.join_endpoint_text.empty(), "join endpoint starts empty");
expect(view.validation_error.empty(), "validation starts clear");
expect(view.session_error == jojo::ErrorCode::none, "session error starts clear");
```

- [ ] **Step 2: Register the test target without implementation**

Add:

```cmake
add_executable(jojo_online_menu_tests tests/test_online_menu.cpp)
target_link_libraries(jojo_online_menu_tests PRIVATE jojo_core)
add_test(NAME jojo_online_menu_tests COMMAND jojo_online_menu_tests)
```

Do not add `src/core/online_menu.cpp` yet.

- [ ] **Step 3: Verify RED in branch CI**

Expected failure on Linux and Windows: `core/online_menu.h` missing. Do not accept unrelated failures as RED evidence.

- [ ] **Step 4: Commit**

```bash
git add tests/test_online_menu.cpp CMakeLists.txt
git commit -m "test: require R2.5 online product flow"
```

---

### Task 2: GREEN view model, Home behavior, and join validation

**Files:**
- Create: `src/core/online_menu.h`
- Create: `src/core/online_menu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_online_menu.cpp`

**Interfaces:**
- Consumes: `OnlineSessionController`, `parse_direct_endpoint`, controller view state.
- Produces this public API:

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

class OnlineMenuSession {
public:
    [[nodiscard]] const OnlineMenuViewState& view() const noexcept { return view_; }
    void set_join_endpoint(std::string text);
    [[nodiscard]] Result<void> start_host(NetworkEndpoint local,
                                          DirectSessionTiming timing = {});
    [[nodiscard]] Result<void> start_join(NetworkEndpoint local,
                                          DirectSessionTiming timing,
                                          std::uint64_t now_ms);
    [[nodiscard]] Result<std::vector<NetworkPacket>> tick(std::uint64_t now_ms);
    [[nodiscard]] Result<void> disconnect(std::uint64_t now_ms);
    void return_home() noexcept;
private:
    friend struct OnlineMenuSessionTestAccess;
    void refresh_view() noexcept;
    OnlineSessionController controller_{};
    OnlineMenuViewState view_{};
};
```

`OnlineMenuSessionTestAccess` is not production API. It is only a friend declaration so the test translation unit can exercise exact packet forwarding without exposing the controller publicly.

- [ ] **Step 1: Extend tests for form state and invalid endpoint**

```cpp
menu.set_join_endpoint("bad-endpoint");
expect(menu.view().join_endpoint_text == "bad-endpoint", "endpoint text stored");

const auto invalid = menu.start_join(jojo::NetworkEndpoint::loopback(0), fast_timing(), 0);
expect(!invalid && invalid.error == jojo::ErrorCode::invalid_argument,
       "malformed join is validation failure");
expect(menu.view().screen == jojo::OnlineMenuScreen::home,
       "malformed join remains home");
expect(!menu.view().validation_error.empty(), "validation detail exposed");
expect(menu.view().session_error == jojo::ErrorCode::none,
       "form validation does not become session fault");

menu.set_join_endpoint("127.0.0.1:34567");
expect(menu.view().validation_error.empty(), "editing clears form error");
```

Also require duplicate/misused Host/Join calls to return `invalid_argument` without overwriting an existing operational fault.

- [ ] **Step 2: Implement state mapping and mirrored fields**

`refresh_view()` copies role/endpoints/telemetry/session error from `controller_.view()` and maps states exactly:

```cpp
inactive         -> home
waiting_for_peer -> hosting
connecting       -> joining
connected        -> connected
reconnecting     -> reconnecting
disconnected     -> disconnected
faulted          -> faulted
```

Recompute permissions on every refresh:

```cpp
view_.can_host = view_.screen == OnlineMenuScreen::home;
view_.can_join = view_.screen == OnlineMenuScreen::home;
view_.can_disconnect = view_.screen == OnlineMenuScreen::connected;
view_.can_return_home =
    view_.screen == OnlineMenuScreen::hosting ||
    view_.screen == OnlineMenuScreen::joining ||
    view_.screen == OnlineMenuScreen::reconnecting ||
    view_.screen == OnlineMenuScreen::disconnected ||
    view_.screen == OnlineMenuScreen::faulted;
view_.can_start_gameplay = controller_.view().can_send_gameplay;
```

Home and Connected both expose `can_return_home == false`.

- [ ] **Step 3: Implement endpoint editing and Join delegation**

`set_join_endpoint()` stores text and clears only `validation_error`.

`start_join()`:
1. requires Home;
2. parses `join_endpoint_text` with `parse_direct_endpoint`;
3. on parse failure, sets `validation_error`, returns that failure, leaves controller inactive;
4. on parse success, clears validation error, calls `controller_.join(...)`, refreshes view, returns controller result.

- [ ] **Step 4: Implement Host delegation and wire source into `jojo_core`**

`start_host()` requires Home, calls `controller_.host(...)`, refreshes view, returns the controller result.

Add `src/core/online_menu.cpp` to `jojo_core`.

- [ ] **Step 5: Verify targeted GREEN**

Run the new menu tests and existing `jojo_online_session_tests`; both must pass on Linux and Windows.

- [ ] **Step 6: Commit**

```bash
git add src/core/online_menu.h src/core/online_menu.cpp tests/test_online_menu.cpp CMakeLists.txt
git commit -m "feat: add portable online product flow state"
```

---

### Task 3: GREEN real Host/Join, `tick()`, packet preservation, and telemetry

**Files:**
- Modify: `tests/test_online_menu.cpp`
- Modify: `src/core/online_menu.cpp`

**Interfaces:**
- Consumes: Task 2 public API.
- Produces: real loopback connection flow and transparent packet return.

- [ ] **Step 1: Add deterministic fast timing and pair helper**

```cpp
jojo::DirectSessionTiming fast_timing() {
    jojo::DirectSessionTiming timing{};
    timing.retry_interval_ms = 5;
    timing.heartbeat_interval_ms = 10;
    timing.liveness_timeout_ms = 30;
    timing.reconnect_timeout_ms = 80;
    return timing;
}
```

Pair helper:
1. Host on `NetworkEndpoint::loopback(0)`;
2. read resolved Host endpoint;
3. format it into Client endpoint text;
4. start Client Join;
5. bounded alternating `tick(now)` calls until both become Connected.

- [ ] **Step 2: Require Hosting/Joining/Connected product state**

Check Host role/ephemeral endpoint, Client remote endpoint, gameplay gate closed before connection and open only when both are Connected, explicit disconnect enabled only Connected, and `can_return_home == false` Connected.

- [ ] **Step 3: Implement `tick()` transparently**

Home/inactive `tick()` returns `invalid_argument` and leaves Home non-faulted.

For active controller state:

```cpp
auto packets = controller_.poll(now_ms);
refresh_view();
if (!packets) {
    return Result<std::vector<NetworkPacket>>::failure(
        packets.error, packets.detail);
}
return packets;
```

No packet filtering, reordering, conversion, or consumption occurs in the menu.

- [ ] **Step 4: Implement test-private controller access**

In `tests/test_online_menu.cpp`, define the friend exactly in namespace `jojo`:

```cpp
namespace jojo {
struct OnlineMenuSessionTestAccess {
    static OnlineSessionController& controller(OnlineMenuSession& menu) noexcept {
        return menu.controller_;
    }
};
}
```

This is the only seam used for exact packet-forwarding verification. Do not add a production accessor.

- [ ] **Step 5: Prove `tick()` preserves gameplay packets exactly**

Connect two menus. Build:

```cpp
jojo::NetworkPacket packet{};
packet.kind = jojo::NetworkPacketKind::input;
packet.sequence = 91;
packet.frame = 77;
packet.payload = {9, 8, 7, 6};
```

Send from Client through:

```cpp
auto& client_controller = jojo::OnlineMenuSessionTestAccess::controller(client);
const auto sent = client_controller.send(packet, 21);
expect(static_cast<bool>(sent), "test peer sends gameplay packet");
```

Then call `host.tick(22)` and require:

```cpp
expect(received && received.value.size() == 1,
       "tick returns exactly one gameplay packet");
expect(received.value.front() == packet,
       "tick preserves gameplay packet exactly");
```

- [ ] **Step 6: Prove telemetry mirrors controller-derived results**

After traffic/ticks, require expected sent/received counters to be nonzero and compare all exposed telemetry fields to the private test-access controller view on the same menu:

```cpp
const auto& source = jojo::OnlineMenuSessionTestAccess::controller(host).view();
expect(host.view().rtt_ms == source.rtt_ms, "RTT mirrors controller");
expect(host.view().jitter_ms == source.jitter_ms, "jitter mirrors controller");
expect(host.view().packet_loss_percent == source.packet_loss_percent,
       "loss mirrors controller");
expect(host.view().packets_sent == source.packets_sent,
       "sent counter mirrors controller");
expect(host.view().packets_received == source.packets_received,
       "received counter mirrors controller");
expect(host.view().packets_lost == source.packets_lost,
       "lost counter mirrors controller");
```

- [ ] **Step 7: Run targeted tests + branch CI and commit**

Require Linux + Windows build/CTest green, including existing online-session tests.

```bash
git add src/core/online_menu.cpp tests/test_online_menu.cpp
git commit -m "feat: connect online product flow to direct sessions"
```

---

### Task 4: GREEN reconnect, disconnect, faults, and return-home semantics

**Files:**
- Modify: `tests/test_online_menu.cpp`
- Modify: `src/core/online_menu.cpp`

**Interfaces:**
- Consumes: Tasks 2-3 menu lifecycle.
- Produces: complete approved lifecycle/reset/error contract.

- [ ] **Step 1: Add reconnect recovery test**

Drive a connected pair past liveness timeout while withholding peer service until one side is `reconnecting`. Require pinned peer retained, gameplay blocked, `can_return_home == true`. Resume both peers and require return to Connected.

- [ ] **Step 2: Add reconnect-timeout test**

Drive a fresh pair to Reconnecting and advance beyond reconnect deadline without recovery. Require Disconnected, `session_error == none`, gameplay blocked, return-home enabled.

- [ ] **Step 3: Implement and test explicit disconnect**

`disconnect(now_ms)` requires Connected, delegates to controller, refreshes, returns controller result. Local side becomes Disconnected immediately; peer becomes Disconnected after its next tick.

All other screens reject `disconnect()` with `invalid_argument` and preserve state.

- [ ] **Step 4: Add deterministic operational-fault test**

Bind one Host to an explicit loopback port and attempt a second Host on the same endpoint. Require the second menu to return `io_error`, map Faulted, preserve error/detail, and reject direct retry with `invalid_argument` until reset.

- [ ] **Step 5: Implement `return_home()` exactly**

```cpp
switch (view_.screen) {
case OnlineMenuScreen::home:
case OnlineMenuScreen::connected:
    return;
case OnlineMenuScreen::hosting:
case OnlineMenuScreen::joining:
case OnlineMenuScreen::reconnecting:
case OnlineMenuScreen::disconnected:
case OnlineMenuScreen::faulted:
    controller_.reset();
    break;
}
const auto endpoint_text = view_.join_endpoint_text;
view_.validation_error.clear();
refresh_view();
view_.join_endpoint_text = endpoint_text;
```

The implementation must preserve endpoint text, clear transient validation/session evidence via reset/refresh, and never silently reset Connected.

- [ ] **Step 6: Test every return-home source state**

Require:
- Home -> no-op;
- Hosting -> Home;
- Joining -> Home;
- Reconnecting -> Home;
- Disconnected -> Home;
- Faulted -> Home;
- Connected -> remains Connected and peer stays alive until explicit Disconnect.

Also require endpoint text retained across every legal reset path and both validation/session errors cleared on Home.

- [ ] **Step 7: Full verification and commit**

Run targeted menu/session tests, then full CTest on Linux and Windows.

```bash
git add src/core/online_menu.cpp tests/test_online_menu.cpp
git commit -m "test: lock online product flow lifecycle"
```

---

### Task 5: Final feature evidence and repository truth checkpoint

**Files:**
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: successful Tasks 1-4 and exact CI/artifact evidence.
- Produces: evidence-aligned R2.5 checkpoint with unchanged readiness semantics.

- [ ] **Step 1: Verify exact functional implementation HEAD**

Require final feature branch CI success for:
- Linux configure/build;
- production-readiness gate;
- full CTest;
- R2.5 direct UDP contract;
- Windows MSVC Release configure/build;
- Windows readiness gate;
- Windows CTest Release;
- Windows R2.5 UDP contract;
- `JOJO-Recompiled-Windows-x64` artifact upload.

Record exact HEAD SHA, run ID, artifact ID, size, and SHA-256 digest.

- [ ] **Step 2: Update `PROJECT-STATE.md`**

Record branch, spec, plan, functional checkpoint, GREEN run, artifact evidence, implemented Direct product-flow scope, and explicit non-goals. Keep R2.5 `implemented-unverified`.

- [ ] **Step 3: Update `docs/NEXT-MILESTONES.md`**

State that Direct Online product flow is implemented and the next honest step is to render/consume it in the real in-game presentation path, followed by game-specific rollback bridging when commercial evidence permits. Do not claim M9 complete.

- [ ] **Step 4: Update only R2.5 evidence if convention requires latest checkpoint**

Keep row shape:

```tsv
R2.5	implemented-unverified	github-actions:run-<successful-run-id>	none
```

- [ ] **Step 5: Commit documentation checkpoint**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "checkpoint: record R2.5 online product flow evidence"
```

- [ ] **Step 6: Verify docs-checkpoint HEAD**

Because docs trigger CI, require the final branch HEAD to pass Linux and Windows again before claiming package completion. Record final docs-run/artifact separately rather than recursively editing evidence.

- [ ] **Step 7: Branch-finishing handoff**

Use the finishing-a-development-branch workflow. Do not merge/update `main` without explicit user authorization.

---

## Plan Self-Review Results

### Spec coverage

The plan explicitly covers Direct-only scope, all seven menu states, form validation, Host/Join delegation, controller-owned lifecycle, transparent `tick()` packet return, telemetry mirroring, gameplay gate, reconnect recovery/timeout, explicit disconnect, operational faults, all `return_home()` semantics, endpoint-text retention, Win32-shell exclusion, CI evidence, and readiness truth boundaries.

### Placeholder scan

No `TBD`, `TODO`, generic “handle errors”, unspecified test seam, or deferred implementation placeholder remains.

### Type consistency

All public names/signatures match the approved spec. The only additional declaration is the private `friend struct OnlineMenuSessionTestAccess`, used solely by `tests/test_online_menu.cpp`; it does not change the production-facing API.