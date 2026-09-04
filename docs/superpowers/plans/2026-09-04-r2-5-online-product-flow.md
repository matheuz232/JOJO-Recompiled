# R2.5 Online Product Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the portable Direct Online product-flow model that exposes Host/Join, lifecycle, telemetry, validation, disconnect/reset behavior, and a gameplay-readiness gate around the integrated `OnlineSessionController`.

**Architecture:** Add `OnlineMenuSession` in `jojo_core` as a pure product/UI state model. It owns exactly one `OnlineSessionController`, maps controller lifecycle into `OnlineMenuScreen`, keeps join-endpoint form text/validation separate from operational session errors, and returns gameplay packets unchanged from `tick()` for a future rollback/gameplay bridge. The first-run Win32 conversion shell remains untouched.

**Tech Stack:** C++20, CMake, CTest, existing `OnlineSessionController`, loopback UDP integration tests, Linux CI, Windows x64/MSVC CI.

**Spec:** `docs/superpowers/specs/2026-09-04-r2-5-online-product-flow-design.md`

## Global Constraints

- Direct Host/Join only; do not add Casual, Ranked, matchmaking, rooms, invites, accounts, relay, NAT traversal, DNS/hostnames, IPv6, encryption, spectator, replay, or service-backed UI.
- `OnlineMenuSession` must not open sockets directly or duplicate reconnect/liveness/telemetry logic.
- `OnlineSessionController` remains the sole owner of Direct session behavior.
- `src/app_win32/main.cpp` must remain the first-run conversion/preparation shell; do not add Online controls there.
- `can_start_gameplay` must always be derived from `OnlineSessionController::view().can_send_gameplay`.
- `tick(now_ms)` must return gameplay packets from controller polling unchanged and in order.
- Form validation errors remain local to the menu and must not fault the controller.
- Operational session errors mirror controller fault semantics.
- Reconnect timeout and normal peer disconnect map to `disconnected`, not `faulted`.
- `return_home()` must never silently disconnect a connected peer.
- Caller supplies `now_ms`; the menu must not read wall-clock time.
- No proprietary game data, extracted assets, commercial fingerprints, saves, or generated commercial content may be committed.
- Generic tests/CI must not promote R2.5 from `implemented-unverified` or claim commercial online compatibility.

---

### Task 1: Portable menu-state contract RED

**Files:**
- Create: `tests/test_online_menu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `OnlineSessionController`, `OnlineSessionState`, `OnlineSessionViewState`, `DirectSessionRole`, `DirectSessionTiming`, `NetworkEndpoint`, `NetworkPacket`, `ErrorCode`.
- Produces requirements for:
  - `enum class OnlineMenuScreen { home, hosting, joining, connected, reconnecting, disconnected, faulted };`
  - `struct OnlineMenuViewState`
  - `class OnlineMenuSession`
  - `OnlineMenuSession::view()`
  - `OnlineMenuSession::set_join_endpoint(std::string)`
  - `OnlineMenuSession::start_host(...)`
  - `OnlineMenuSession::start_join(...)`
  - `OnlineMenuSession::tick(...)`
  - `OnlineMenuSession::disconnect(...)`
  - `OnlineMenuSession::return_home()`.

- [ ] **Step 1: Create the RED test target and initial contract test**

Add `tests/test_online_menu.cpp` with a tiny test harness matching the existing `test_online_session.cpp` style and include `core/online_menu.h`.

The first assertions must require this exact initial state:

```cpp
jojo::OnlineMenuSession menu;
const auto& view = menu.view();
expect(view.screen == jojo::OnlineMenuScreen::home, "initial screen is home");
expect(view.can_host, "home allows host");
expect(view.can_join, "home allows join");
expect(!view.can_disconnect, "home cannot disconnect");
expect(!view.can_return_home, "home has no back transition");
expect(!view.can_start_gameplay, "home blocks gameplay");
expect(view.join_endpoint_text.empty(), "join field starts empty");
expect(view.validation_error.empty(), "validation error starts empty");
expect(view.session_error == jojo::ErrorCode::none, "session error starts clear");
```

- [ ] **Step 2: Register the RED test in CMake**

Add:

```cmake
add_executable(jojo_online_menu_tests tests/test_online_menu.cpp)
target_link_libraries(jojo_online_menu_tests PRIVATE jojo_core)
add_test(NAME jojo_online_menu_tests COMMAND jojo_online_menu_tests)
```

Do not add `src/core/online_menu.cpp` to `jojo_core` yet.

- [ ] **Step 3: Push RED and verify the expected failure**

Expected Linux/Windows failure: compile error because `core/online_menu.h` does not exist.

Acceptance evidence:
- Linux reaches the new target and fails at missing header.
- Windows reaches the new target and fails at missing header.
- No unrelated failure is accepted as RED evidence.

- [ ] **Step 4: Commit the RED checkpoint**

```bash
git add tests/test_online_menu.cpp CMakeLists.txt
git commit -m "test: require R2.5 online product flow"
```

---

### Task 2: View model, endpoint editing, and Home semantics GREEN

**Files:**
- Create: `src/core/online_menu.h`
- Create: `src/core/online_menu.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_online_menu.cpp`

**Interfaces:**
- Consumes: `OnlineSessionController::view()`, `parse_direct_endpoint`, `format_direct_endpoint`.
- Produces:

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
```

and:

```cpp
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
    void refresh_view() noexcept;
    OnlineSessionController controller_{};
    OnlineMenuViewState view_{};
};
```

- [ ] **Step 1: Extend tests for endpoint text and local validation**

Add assertions:

```cpp
menu.set_join_endpoint("bad-endpoint");
expect(menu.view().join_endpoint_text == "bad-endpoint", "endpoint text stored");

const auto invalid = menu.start_join(jojo::NetworkEndpoint::loopback(0), fast_timing(), 0);
expect(!invalid, "malformed join is rejected");
expect(invalid.error == jojo::ErrorCode::invalid_argument, "malformed join uses invalid_argument");
expect(menu.view().screen == jojo::OnlineMenuScreen::home, "malformed join stays home");
expect(!menu.view().validation_error.empty(), "malformed join exposes validation detail");
expect(menu.view().session_error == jojo::ErrorCode::none, "form error does not become session fault");
expect(menu.view().can_host && menu.view().can_join, "form error keeps actions enabled");

menu.set_join_endpoint("127.0.0.1:34567");
expect(menu.view().validation_error.empty(), "editing clears prior validation error");
```

Also test duplicate/misused `start_host`/`start_join` attempts return `invalid_argument` without fabricating a new fault.

- [ ] **Step 2: Implement the header and initial view refresh rules**

`refresh_view()` must copy controller fields and set permissions by mapped state:

```cpp
switch (controller_.view().state) {
case OnlineSessionState::inactive:
    view_.screen = OnlineMenuScreen::home;
    break;
case OnlineSessionState::waiting_for_peer:
    view_.screen = OnlineMenuScreen::hosting;
    break;
case OnlineSessionState::connecting:
    view_.screen = OnlineMenuScreen::joining;
    break;
case OnlineSessionState::connected:
    view_.screen = OnlineMenuScreen::connected;
    break;
case OnlineSessionState::reconnecting:
    view_.screen = OnlineMenuScreen::reconnecting;
    break;
case OnlineSessionState::disconnected:
    view_.screen = OnlineMenuScreen::disconnected;
    break;
case OnlineSessionState::faulted:
    view_.screen = OnlineMenuScreen::faulted;
    break;
}
```

Permissions must be recomputed every refresh:

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

`home` and `connected` must both expose `can_return_home == false`.

- [ ] **Step 3: Implement endpoint editing and join validation**

`set_join_endpoint` stores the string and clears only `validation_error`.

`start_join` must:
1. reject unless current screen is Home;
2. parse `view_.join_endpoint_text` with `parse_direct_endpoint`;
3. on parse failure, set `validation_error = parsed.detail` and return the parser failure without calling the controller;
4. on success, clear `validation_error`, delegate to `controller_.join`, call `refresh_view()`, and return the controller result.

- [ ] **Step 4: Implement Host delegation and add source to CMake**

`start_host` must reject outside Home, delegate to `controller_.host`, call `refresh_view()`, and preserve controller fault semantics.

Add `src/core/online_menu.cpp` to `jojo_core` in `CMakeLists.txt`.

- [ ] **Step 5: Run targeted GREEN tests**

Run:

```bash
ctest --test-dir build --output-on-failure -R "jojo_online_menu_tests|jojo_online_session_tests"
```

Expected: both tests pass on Linux and Windows.

- [ ] **Step 6: Commit the GREEN checkpoint**

```bash
git add src/core/online_menu.h src/core/online_menu.cpp tests/test_online_menu.cpp CMakeLists.txt
git commit -m "feat: add portable online product flow state"
```

---

### Task 3: Real Host/Join, tick packet preservation, and telemetry GREEN

**Files:**
- Modify: `tests/test_online_menu.cpp`
- Modify: `src/core/online_menu.cpp`

**Interfaces:**
- Consumes: Task 2 `OnlineMenuSession` API.
- Produces: real loopback lifecycle through `start_host`, `start_join`, and `tick`, plus exact packet preservation.

- [ ] **Step 1: Add loopback pair helpers**

Mirror the existing fast timing profile from `test_online_session.cpp`:

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

Create a helper that:
1. starts Host on loopback port 0;
2. reads Host `local_endpoint` from its view;
3. writes that formatted endpoint into Client `join_endpoint_text`;
4. calls Client `start_join`;
5. repeatedly calls `host.tick(now)` and `client.tick(now)` until both screens are Connected or a bounded iteration count is exhausted.

- [ ] **Step 2: Add assertions for Hosting/Joining/Connected mapping**

Require:

```cpp
expect(host.view().screen == jojo::OnlineMenuScreen::hosting, "host maps waiting to hosting");
expect(host.view().role == jojo::DirectSessionRole::host, "host role visible");
expect(host.view().local_endpoint && host.view().local_endpoint->port != 0,
       "host ephemeral endpoint visible");
expect(!host.view().can_start_gameplay, "hosting blocks gameplay");

expect(client.view().screen == jojo::OnlineMenuScreen::joining, "client maps connecting to joining");
expect(client.view().remote_endpoint == host.view().local_endpoint, "join target visible");

expect(host.view().screen == jojo::OnlineMenuScreen::connected, "host connected");
expect(client.view().screen == jojo::OnlineMenuScreen::connected, "client connected");
expect(host.view().can_start_gameplay && client.view().can_start_gameplay,
       "connected opens gameplay readiness gate");
expect(host.view().can_disconnect && client.view().can_disconnect,
       "connected enables explicit disconnect");
expect(!host.view().can_return_home && !client.view().can_return_home,
       "connected cannot silently return home");
```

- [ ] **Step 3: Implement `tick()` as a transparent controller poll**

`tick(now_ms)` must reject Home/inactive with `invalid_argument` and preserve Home without faulting.

For active sessions:

```cpp
auto packets = controller_.poll(now_ms);
refresh_view();
if (!packets) return Result<std::vector<NetworkPacket>>::failure(packets.error, packets.detail);
return packets;
```

Do not filter packet kinds in the menu layer.

- [ ] **Step 4: Add gameplay-packet preservation test**

After connecting Host/Client, construct an input packet:

```cpp
jojo::NetworkPacket packet{};
packet.kind = jojo::NetworkPacketKind::input;
packet.sequence = 91;
packet.frame = 77;
packet.payload = {9, 8, 7, 6};
```

Send it through the already integrated lower-level controller path available to the test. If direct access to the menu-owned controller is intentionally unavailable, use the peer menu flow and lower-level network API only through a test seam that does not become public product API. The final assertion must be:

```cpp
expect(received.value.size() == 1, "tick returns exactly one gameplay packet");
expect(received.value.front() == packet, "tick preserves gameplay packet exactly");
```

If a minimal test-only seam is needed, keep it private to the test translation unit and do not expose mutable controller ownership in `OnlineMenuSession` public API.

- [ ] **Step 5: Add telemetry mirror assertions**

After traffic/ticks, require menu counters and RTT/jitter/loss to equal the underlying controller-derived values that are observable through the resulting flow. At minimum assert sent/received counters become nonzero on the expected peers and packet loss remains numerically consistent with controller behavior.

- [ ] **Step 6: Run targeted tests and full branch CI**

Expected:
- `jojo_online_menu_tests` passes;
- `jojo_online_session_tests` remains green;
- Linux and Windows/MSVC build and CTest pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/online_menu.cpp tests/test_online_menu.cpp
git commit -m "feat: connect online product flow to direct sessions"
```

---

### Task 4: Reconnect, disconnect, fault, and return-home lifecycle GREEN

**Files:**
- Modify: `tests/test_online_menu.cpp`
- Modify: `src/core/online_menu.cpp`

**Interfaces:**
- Consumes: Tasks 2-3 lifecycle and tick behavior.
- Produces: complete state/reset contract required by the design spec.

- [ ] **Step 1: Add reconnect transition tests**

Using a connected loopback pair and `fast_timing()`, drive one side past liveness timeout without servicing its peer until the menu maps to `reconnecting`.

Require:

```cpp
expect(menu.view().screen == jojo::OnlineMenuScreen::reconnecting,
       "silence maps to reconnecting");
expect(!menu.view().can_start_gameplay, "reconnecting blocks gameplay");
expect(menu.view().remote_endpoint == pinned_peer, "reconnecting preserves pinned peer");
expect(menu.view().can_return_home, "reconnecting allows local cancel");
```

Then service both sides and require recovery to `connected`.

- [ ] **Step 2: Add reconnect-timeout test**

Drive a fresh pair into reconnecting and advance beyond `reconnect_timeout_ms` without peer recovery.

Require:

```cpp
expect(menu.view().screen == jojo::OnlineMenuScreen::disconnected,
       "reconnect timeout maps to disconnected");
expect(menu.view().session_error == jojo::ErrorCode::none,
       "reconnect timeout is not a fault");
expect(menu.view().can_return_home, "disconnected can return home");
```

- [ ] **Step 3: Implement and test explicit disconnect**

`disconnect(now_ms)` must:
1. require Connected;
2. delegate to `controller_.disconnect(now_ms)`;
3. refresh the view;
4. return the controller result.

Require the local side to become Disconnected immediately and the peer to become Disconnected after its next `tick()`.

Misuse while Home/Hosting/Joining/Reconnecting/Disconnected/Faulted must return `invalid_argument` and preserve lifecycle state.

- [ ] **Step 4: Add deterministic operational-fault test**

Use a loopback bind collision, matching the existing `OnlineSessionController` fault-contract strategy:
1. start one Host on an explicit loopback port;
2. start a second menu Host on the same explicit endpoint;
3. require failure with `io_error`;
4. require second menu screen `faulted`;
5. require `session_error == io_error` and non-empty detail;
6. require direct retry without reset to fail with `invalid_argument` while preserving the original fault.

- [ ] **Step 5: Implement `return_home()` exactly by screen**

Rules:

```cpp
switch (view_.screen) {
case OnlineMenuScreen::home:
    return;
case OnlineMenuScreen::connected:
    return; // explicit disconnect required first
case OnlineMenuScreen::hosting:
case OnlineMenuScreen::joining:
case OnlineMenuScreen::reconnecting:
case OnlineMenuScreen::disconnected:
case OnlineMenuScreen::faulted:
    controller_.reset();
    break;
}
```

After reset:
- preserve `join_endpoint_text`;
- clear `validation_error`;
- clear role/endpoints/telemetry/session error through controller reset + refresh;
- set Home permissions;
- keep gameplay blocked.

- [ ] **Step 6: Add return-home tests for every legal source screen**

Required cases:
- Home -> no-op;
- Hosting -> Home;
- Joining -> Home;
- Reconnecting -> Home;
- Disconnected -> Home;
- Faulted -> Home;
- Connected -> stays Connected and peer remains alive until explicit `disconnect`.

Also require endpoint text retention across legal return-home reset:

```cpp
menu.set_join_endpoint("127.0.0.1:34567");
// drive into resettable state
menu.return_home();
expect(menu.view().join_endpoint_text == "127.0.0.1:34567",
       "return home retains endpoint text");
expect(menu.view().validation_error.empty(), "return home clears validation error");
expect(menu.view().session_error == jojo::ErrorCode::none,
       "return home clears session fault");
```

- [ ] **Step 7: Run targeted and full tests**

Run the new menu test and existing session test first, then full CTest on Linux and Windows.

Expected: all green.

- [ ] **Step 8: Commit**

```bash
git add src/core/online_menu.cpp tests/test_online_menu.cpp
git commit -m "test: lock online product flow lifecycle"
```

---

### Task 5: Package verification and truth-boundary checkpoint

**Files:**
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: successful implementation commits and CI evidence from Tasks 1-4.
- Produces: evidence-aligned repository truth for the new R2.5 product-flow checkpoint.

- [ ] **Step 1: Run the final feature-branch CI on the exact implementation HEAD**

Require:
- Linux configure/build success;
- Linux production-readiness gate success;
- Linux CTest success;
- Linux R2.5 direct UDP contract success;
- Windows MSVC configure/build Release success;
- Windows production-readiness gate success;
- Windows CTest Release success;
- Windows R2.5 direct UDP contract success;
- single `JOJO-Recompiled-Windows-x64` artifact upload success.

Record run ID, exact feature HEAD SHA, artifact ID, size, and SHA-256 digest.

- [ ] **Step 2: Update `PROJECT-STATE.md` without overstating readiness**

Add an R2.5 Online Product Flow checkpoint containing:
- branch name;
- spec path;
- plan path;
- functional/test checkpoint SHA;
- GREEN workflow run;
- Windows artifact ID/digest;
- implemented scope: Direct-only product flow, Host/Join state, validation, lifecycle mapping, telemetry exposure, packet preservation, disconnect/reset semantics;
- explicit truth boundary: no rendered commercial menu, matchmaking, ranked, rooms, relay/NAT, accounts, or commercial rollback integration.

Keep R2.5 status `implemented-unverified`.

- [ ] **Step 3: Update `docs/NEXT-MILESTONES.md`**

State that the portable Direct Online product flow is implemented on its feature branch and that the next honest increment is rendering/consuming it in the real in-game presentation path when that path exists, then game-specific rollback bridging when commercial integration evidence is available.

Do not state that M9 is complete.

- [ ] **Step 4: Update readiness evidence only after successful feature CI**

In `docs/architecture/PRODUCTION-READINESS.tsv`, update only the R2.5 evidence field to the successful feature workflow if repository convention requires latest checkpoint evidence.

The row must remain:

```tsv
R2.5	implemented-unverified	github-actions:run-<successful-run-id>	none
```

- [ ] **Step 5: Commit the documentation checkpoint**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "checkpoint: record R2.5 online product flow evidence"
```

- [ ] **Step 6: Verify the docs-checkpoint HEAD again**

Because docs changes trigger CI, require the final branch HEAD to pass the same Linux and Windows pipeline before claiming the package complete.

Record the final run and artifact separately from the functional checkpoint if necessary to avoid recursive documentation-evidence churn.

- [ ] **Step 7: Integration handoff**

After final green verification, use the branch-finishing workflow. Do not update `main` without explicit user authorization.

---

## Plan Self-Review Results

### Spec coverage

Covered explicitly:
- Direct-only scope;
- portable `OnlineMenuSession` boundary;
- all seven product screens;
- endpoint text and validation separation;
- Host/Join delegation;
- controller-owned lifecycle/telemetry;
- `tick()` packet preservation;
- gameplay readiness gate;
- reconnect/recovery/timeout;
- explicit disconnect;
- operational fault persistence;
- `return_home()` semantics for every screen;
- endpoint-text retention;
- no Win32 conversion-shell Online UI;
- CI/evidence and readiness truth boundary.

### Placeholder scan

No `TBD`, `TODO`, “implement later”, “similar to”, or unspecified generic error-handling steps remain.

### Type consistency

The plan uses the same public names and signatures as the approved spec:
- `OnlineMenuScreen`
- `OnlineMenuViewState`
- `OnlineMenuSession`
- `set_join_endpoint`
- `start_host`
- `start_join`
- `tick`
- `disconnect`
- `return_home`.

One implementation-sensitive point is intentionally constrained rather than exposed as public API: the packet-preservation integration test must not add a mutable controller accessor merely for testing. If a seam is necessary, it stays test-private.