# R2.5 Online Session Controller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a UI-independent `OnlineSessionController` that wraps the existing direct UDP session with direct IPv4 endpoint parsing, stable product lifecycle/view state, gameplay gating, reconnect/disconnect/reset behavior, and user-presentable errors.

**Architecture:** `OnlineSessionController` owns one optional `DirectUdpSession` and never reimplements protocol, reliability, liveness, spoof protection, or telemetry math. A pure endpoint parser/formatter and immutable `OnlineSessionViewState` form the future UI boundary; controller methods translate product actions into `DirectUdpSession` calls and derive product state from transport state using caller-supplied time only.

**Tech Stack:** C++20, existing `jojo_core`, `DirectUdpSession`, real loopback IPv4 UDP sockets, CMake/CTest, GitHub Actions Linux + Windows/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-04-r2-5-online-controller-design.md`

## Global Constraints

- Work on `feature/r2-5-online-controller` until the package is reviewed and GREEN.
- Do not modify the first-run Win32 preparation UI or `SettingsMenuSession` for this package.
- Do not implement or claim matchmaking, casual/ranked backend services, public rooms, invitations, accounts, profiles/history, NAT traversal, relay, DNS/hostname resolution, encryption/authentication, spectator mode, replays, game-specific commercial online integration, or complete M9 UI.
- Keep all time-dependent behavior caller-driven through `now_ms`; do not read wall-clock time in the controller.
- Keep handshake, retransmission, heartbeat/liveness, reconnect, spoof protection, disconnect control, and telemetry math owned by `DirectUdpSession`.
- Keep gameplay send/delivery allowed only while the product state is `connected`.
- Validation/lifecycle misuse must not poison an otherwise healthy state; operational I/O/runtime failures must enter `faulted` and require `reset()` before a new session.
- `reset()` is local-only, valid from every product state, and may cancel waiting/connecting/reconnecting without pretending to notify a peer.
- Keep R2.5 `implemented-unverified`; generic tests do not prove commercial-game online compatibility.
- Preserve R2.2/R2.4 external-evidence blockers.
- Keep one shipping executable: `JOJO-Recompiled.exe`.
- Mods remain deferred until base-game production completion.

## File Structure

- Create `src/core/online_session.h` — public product-facing online state, endpoint parser/formatter declarations, view snapshot, controller API.
- Create `src/core/online_session.cpp` — endpoint parsing/formatting, lifecycle translation, view refresh, fault handling, and delegation to `DirectUdpSession`.
- Create `tests/test_online_session.cpp` — focused product-layer contract using real loopback UDP sockets and deterministic caller-supplied time.
- Modify `CMakeLists.txt` — compile `online_session.cpp` into `jojo_core` and register `jojo_online_session_tests` as a normal CTest target.
- Modify `PROJECT-STATE.md` only at the final checkpoint — record the GREEN controller evidence without changing R2.5 verification status.
- Modify `docs/NEXT-MILESTONES.md` only at the final checkpoint — record that the direct-session product controller exists while keeping service/UI non-goals explicit.
- Modify `docs/architecture/PRODUCTION-READINESS.tsv` only at the final checkpoint — update R2.5 evidence to the new full package CI run while retaining `implemented-unverified`.

---

### Task 1: Endpoint parser, product types, and build registration

**Files:**
- Create: `src/core/online_session.h`
- Create: `src/core/online_session.cpp`
- Create: `tests/test_online_session.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `jojo::NetworkEndpoint`, `jojo::DirectSessionRole`, `jojo::DirectSessionTiming`, `jojo::NetworkPacket`, `jojo::ErrorCode`, `jojo::Result<T>`.
- Produces:

```cpp
enum class OnlineSessionState {
    inactive,
    waiting_for_peer,
    connecting,
    connected,
    reconnecting,
    disconnected,
    faulted,
};

struct OnlineSessionViewState {
    OnlineSessionState state{OnlineSessionState::inactive};
    std::optional<DirectSessionRole> role{};
    std::optional<NetworkEndpoint> local_endpoint{};
    std::optional<NetworkEndpoint> remote_endpoint{};
    double rtt_ms{};
    double jitter_ms{};
    double packet_loss_percent{};
    std::uint64_t packets_sent{};
    std::uint64_t packets_received{};
    std::uint64_t packets_lost{};
    bool can_send_gameplay{};
    ErrorCode last_error{ErrorCode::none};
    std::string last_error_detail{};
};

[[nodiscard]] Result<NetworkEndpoint> parse_direct_endpoint(std::string_view text);
[[nodiscard]] std::string format_direct_endpoint(NetworkEndpoint endpoint);
```

- [ ] **Step 1: Register the new source and test target in CMake before implementation**

Add `src/core/online_session.cpp` to `add_library(jojo_core STATIC ...)` immediately after `src/core/network_protocol.cpp`.

Add the test target near the existing rollback/network-related tests:

```cmake
add_executable(jojo_online_session_tests tests/test_online_session.cpp)
target_link_libraries(jojo_online_session_tests PRIVATE jojo_core)
add_test(NAME jojo_online_session_tests COMMAND jojo_online_session_tests)
```

- [ ] **Step 2: Write the initial RED parser contract**

Create `tests/test_online_session.cpp` with a tiny assertion harness and parser-only cases. The tests must include these exact accepted/rejected forms:

```cpp
#include "core/online_session.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_endpoint_parser() {
    const auto parsed = jojo::parse_direct_endpoint("127.0.0.1:34567");
    expect(static_cast<bool>(parsed), "canonical endpoint parses");
    if (parsed) {
        expect(parsed.value == jojo::NetworkEndpoint::loopback(34567),
               "parsed endpoint value matches");
        expect(jojo::format_direct_endpoint(parsed.value) == "127.0.0.1:34567",
               "endpoint formats canonically");
    }

    for (const std::string bad : {
             "", "127.0.0.1", "127.0.0.1:", "127.0.0.1:0",
             "127.0.0.1:65536", "256.0.0.1:1234", "127.0.0:1234",
             "127.0.0.1.2:1234", "host:1234", "127.0.0.1 :1234",
             "127.0.0.1: 1234", "::1:1234"}) {
        const auto result = jojo::parse_direct_endpoint(bad);
        expect(!result, ("reject endpoint: " + bad).c_str());
        if (!result) {
            expect(result.error == jojo::ErrorCode::invalid_argument,
                   "malformed endpoint uses invalid_argument");
            expect(!result.detail.empty(), "malformed endpoint has detail");
        }
    }
}
} // namespace

int main() {
    test_endpoint_parser();
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Run the new target and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_online_session_tests --parallel 2
```

Expected: build fails because `core/online_session.h` and the parser API do not yet exist.

- [ ] **Step 4: Add the minimal public header**

Create `src/core/online_session.h` with these declarations:

```cpp
#pragma once

#include "core/network_transport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

enum class OnlineSessionState {
    inactive,
    waiting_for_peer,
    connecting,
    connected,
    reconnecting,
    disconnected,
    faulted,
};

struct OnlineSessionViewState {
    OnlineSessionState state{OnlineSessionState::inactive};
    std::optional<DirectSessionRole> role{};
    std::optional<NetworkEndpoint> local_endpoint{};
    std::optional<NetworkEndpoint> remote_endpoint{};
    double rtt_ms{};
    double jitter_ms{};
    double packet_loss_percent{};
    std::uint64_t packets_sent{};
    std::uint64_t packets_received{};
    std::uint64_t packets_lost{};
    bool can_send_gameplay{};
    ErrorCode last_error{ErrorCode::none};
    std::string last_error_detail{};
};

[[nodiscard]] Result<NetworkEndpoint> parse_direct_endpoint(std::string_view text);
[[nodiscard]] std::string format_direct_endpoint(NetworkEndpoint endpoint);

class OnlineSessionController;

} // namespace jojo
```

- [ ] **Step 5: Implement the pure parser/formatter minimally**

Create `src/core/online_session.cpp`. Use manual decimal parsing rather than locale-sensitive streams or hostname APIs. The parser must:

1. require exactly one `:` separator;
2. reject an empty address or port;
3. reject any whitespace;
4. split the address into exactly four non-empty decimal octets;
5. reject non-digits and values above 255;
6. parse a decimal port and reject 0 or values above 65535;
7. return `ErrorCode::invalid_argument` for all user-input failures.

Use stable details from this set:

```cpp
"direct endpoint must use A.B.C.D:PORT"
"direct endpoint IPv4 octet is invalid"
"direct endpoint port is invalid"
```

`format_direct_endpoint()` must emit decimal octets and the decimal port with no extra whitespace.

- [ ] **Step 6: Run parser target GREEN**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt src/core/online_session.h src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: add direct online endpoint contract"
```

---

### Task 2: Host/join lifecycle and stable view snapshot

**Files:**
- Modify: `src/core/online_session.h`
- Modify: `src/core/online_session.cpp`
- Modify: `tests/test_online_session.cpp`

**Interfaces:**
- Consumes: Task 1 product types/parser; `DirectUdpSession::bind`, `DirectUdpSession::connect`, `DirectUdpSession::poll`, `DirectUdpSession::local_endpoint`, `DirectUdpSession::remote_endpoint`, `DirectUdpSession::telemetry`, `DirectUdpSession::state`.
- Produces:

```cpp
class OnlineSessionController {
public:
    [[nodiscard]] const OnlineSessionViewState& view() const noexcept;

    [[nodiscard]] Result<void> host(
        NetworkEndpoint local,
        DirectSessionTiming timing = {});

    [[nodiscard]] Result<void> join(
        NetworkEndpoint local,
        NetworkEndpoint remote,
        DirectSessionTiming timing,
        std::uint64_t now_ms);

    [[nodiscard]] Result<std::vector<NetworkPacket>> poll(std::uint64_t now_ms);

    [[nodiscard]] Result<void> send(const NetworkPacket& packet,
                                    std::uint64_t now_ms);
    [[nodiscard]] Result<void> disconnect(std::uint64_t now_ms);
    void reset() noexcept;

private:
    void refresh_view() noexcept;
    void set_fault(ErrorCode error, std::string detail);

    std::optional<DirectUdpSession> session_{};
    OnlineSessionViewState view_{};
};
```

- [ ] **Step 1: Add RED tests for host/join and view state**

Extend `tests/test_online_session.cpp` with helpers:

```cpp
jojo::DirectSessionTiming fast_timing() {
    jojo::DirectSessionTiming timing{};
    timing.retry_interval_ms = 5;
    timing.heartbeat_interval_ms = 10;
    timing.liveness_timeout_ms = 30;
    timing.reconnect_timeout_ms = 80;
    return timing;
}

bool drive_connected(jojo::OnlineSessionController& host,
                     jojo::OnlineSessionController& client) {
    for (std::uint64_t now = 1; now <= 20; ++now) {
        const auto host_poll = host.poll(now);
        const auto client_poll = client.poll(now);
        if (!host_poll || !client_poll) return false;
        if (host.view().state == jojo::OnlineSessionState::connected &&
            client.view().state == jojo::OnlineSessionState::connected) {
            return true;
        }
    }
    return false;
}
```

Add tests that:

```cpp
void test_host_join_view() {
    jojo::OnlineSessionController host;
    const auto host_started = host.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(static_cast<bool>(host_started), "host binds");
    expect(host.view().state == jojo::OnlineSessionState::waiting_for_peer,
           "host waits for peer");
    expect(host.view().role == jojo::DirectSessionRole::host, "host role exposed");
    expect(host.view().local_endpoint.has_value(), "host local endpoint exposed");
    expect(host.view().local_endpoint && host.view().local_endpoint->port != 0,
           "ephemeral host port resolved");
    expect(!host.view().remote_endpoint, "host has no peer before hello");
    expect(!host.view().can_send_gameplay, "host cannot send while waiting");

    jojo::OnlineSessionController client;
    const auto joined = client.join(
        jojo::NetworkEndpoint::loopback(0),
        *host.view().local_endpoint,
        fast_timing(), 0);
    expect(static_cast<bool>(joined), "client join starts");
    expect(client.view().state == jojo::OnlineSessionState::connecting,
           "client exposes connecting");
    expect(client.view().role == jojo::DirectSessionRole::client,
           "client role exposed");
    expect(client.view().remote_endpoint == host.view().local_endpoint,
           "client target exposed immediately");

    expect(drive_connected(host, client), "controllers connect through polling");
    expect(host.view().state == jojo::OnlineSessionState::connected,
           "host reaches connected");
    expect(client.view().state == jojo::OnlineSessionState::connected,
           "client reaches connected");
    expect(host.view().remote_endpoint == client.view().local_endpoint,
           "host exposes pinned client endpoint");
    expect(host.view().can_send_gameplay && client.view().can_send_gameplay,
           "gameplay gate opens only when connected");
}
```

Also add a lifecycle validation case that `host()` or `join()` called twice returns `invalid_argument` without changing the existing state.

- [ ] **Step 2: Run and verify RED**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
```

Expected: build fails because `OnlineSessionController` methods are not defined.

- [ ] **Step 3: Implement host/join and view derivation**

Implement `OnlineSessionController` with these rules:

```cpp
const OnlineSessionViewState& OnlineSessionController::view() const noexcept {
    return view_;
}
```

`host()`:

```cpp
if (view_.state != OnlineSessionState::inactive) {
    return Result<void>::failure(ErrorCode::invalid_argument,
                                 "online session is already active");
}
auto bound = DirectUdpSession::bind(DirectSessionRole::host, local, timing);
if (!bound) {
    set_fault(bound.error, bound.detail);
    return Result<void>::failure(bound.error, bound.detail);
}
session_.emplace(std::move(bound.value));
view_.role = DirectSessionRole::host;
refresh_view();
return Result<void>::success();
```

`join()` must reject `remote.port == 0` before binding with:

```cpp
return Result<void>::failure(ErrorCode::invalid_argument,
                             "online join remote port must be non-zero");
```

Then bind client, call `connect(remote, now_ms)`, fault on operational failure, and call `refresh_view()` on success.

`refresh_view()` must copy the transport endpoints/telemetry and map states exactly:

```cpp
switch (session_->state()) {
case DirectSessionState::idle:
    view_.state = view_.role == DirectSessionRole::host
        ? OnlineSessionState::waiting_for_peer
        : OnlineSessionState::inactive;
    break;
case DirectSessionState::connecting:
    view_.state = OnlineSessionState::connecting;
    break;
case DirectSessionState::connected:
    view_.state = OnlineSessionState::connected;
    break;
case DirectSessionState::reconnecting:
    view_.state = OnlineSessionState::reconnecting;
    break;
case DirectSessionState::disconnected:
    view_.state = OnlineSessionState::disconnected;
    break;
}
```

Then set:

```cpp
view_.local_endpoint = session_->local_endpoint();
view_.remote_endpoint = session_->remote_endpoint();
const auto& telemetry = session_->telemetry();
view_.rtt_ms = telemetry.rtt_ms;
view_.jitter_ms = telemetry.jitter_ms;
view_.packet_loss_percent = telemetry.packet_loss_percent();
view_.packets_sent = telemetry.packets_sent;
view_.packets_received = telemetry.packets_received;
view_.packets_lost = telemetry.packets_lost;
view_.can_send_gameplay = view_.state == OnlineSessionState::connected;
```

Do not clear `last_error` inside `refresh_view()`; successful `host`, `join`, and `reset` are responsible for starting with a clean error snapshot.

`poll()` delegates to `session_->poll(now_ms)`, faults on operational failure, otherwise refreshes and returns delivered packets.

- [ ] **Step 4: Run Task 2 GREEN**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/core/online_session.h src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: add online host join lifecycle"
```

---

### Task 3: Gameplay roundtrip, reconnect mapping, disconnect, reset, and spoof resistance

**Files:**
- Modify: `src/core/online_session.cpp`
- Modify: `tests/test_online_session.cpp`

**Interfaces:**
- Consumes: Task 2 controller API and real `DirectUdpSession` behavior.
- Produces: fully working `send`, `disconnect`, `reset`, telemetry refresh, reconnect/disconnected mapping, and product-layer peer stability.

- [ ] **Step 1: Add RED gameplay and lifecycle tests**

Add a gameplay roundtrip after `drive_connected()`:

```cpp
jojo::NetworkPacket packet{};
packet.kind = jojo::NetworkPacketKind::input;
packet.sequence = 77;
packet.frame = 42;
packet.payload = {1, 2, 3, 4};

const auto sent = client.send(packet, 21);
expect(static_cast<bool>(sent), "connected client sends gameplay");
const auto delivered = host.poll(22);
expect(static_cast<bool>(delivered), "host poll succeeds after gameplay send");
expect(delivered && delivered.value.size() == 1,
       "host receives exactly one gameplay packet");
if (delivered && delivered.value.size() == 1) {
    expect(delivered.value.front() == packet, "gameplay packet preserved");
}
```

Add misuse checks before connection and during reconnect:

```cpp
const auto bad_send = idle_controller.send(packet, 0);
expect(!bad_send && bad_send.error == jojo::ErrorCode::invalid_argument,
       "send while inactive is validation failure");
expect(idle_controller.view().state == jojo::OnlineSessionState::inactive,
       "send misuse does not fault inactive controller");
```

Add reconnect mapping with `fast_timing()` using monotonic times. After connecting and draining normal traffic, stop polling the peer and advance one side beyond the 30 ms liveness timeout. Assert that side maps to `reconnecting`, `can_send_gameplay == false`, and its pinned remote endpoint remains unchanged. Then advance/poll the client until it also times out and emits a pinned reconnect hello; poll the host and client until both return to `connected`.

Use this explicit sequence as the starting point, adjusting only if queued heartbeat traffic requires one extra drain poll:

```cpp
const auto pinned = host.view().remote_endpoint;
expect(static_cast<bool>(host.poll(60)), "host liveness poll succeeds");
expect(host.view().state == jojo::OnlineSessionState::reconnecting,
       "host maps silence to reconnecting");
expect(host.view().remote_endpoint == pinned, "host keeps pinned peer");
expect(!host.view().can_send_gameplay, "reconnect suppresses gameplay");

expect(static_cast<bool>(client.poll(60)), "client processes queued traffic");
expect(static_cast<bool>(client.poll(100)), "client reaches reconnect attempt");
expect(static_cast<bool>(host.poll(101)), "host accepts pinned reconnect hello");
expect(static_cast<bool>(client.poll(102)), "client accepts reconnect response");
expect(host.view().state == jojo::OnlineSessionState::connected,
       "host recovers connected");
expect(client.view().state == jojo::OnlineSessionState::connected,
       "client recovers connected");
```

Add reconnect-timeout coverage by creating a fresh pair, driving connected, silencing the peer, entering reconnecting, then advancing the reconnecting controller by more than `reconnect_timeout_ms` without a valid pinned-peer handshake. Assert normal `disconnected`, not `faulted`.

- [ ] **Step 2: Add RED explicit disconnect and reset tests**

After connecting a fresh pair:

```cpp
const auto disconnected = client.disconnect(30);
expect(static_cast<bool>(disconnected), "local disconnect succeeds");
expect(client.view().state == jojo::OnlineSessionState::disconnected,
       "local disconnect maps immediately");
expect(!client.view().can_send_gameplay, "disconnect closes gameplay gate");

const auto peer_poll = host.poll(31);
expect(static_cast<bool>(peer_poll), "peer consumes disconnect");
expect(host.view().state == jojo::OnlineSessionState::disconnected,
       "remote disconnect maps normally");
```

Test `reset()` from `waiting_for_peer`, `connecting`, `reconnecting`, `disconnected`, and `faulted` as states become available. Every reset must restore this exact baseline:

```cpp
expect(controller.view().state == jojo::OnlineSessionState::inactive,
       "reset returns inactive");
expect(!controller.view().role, "reset clears role");
expect(!controller.view().local_endpoint, "reset clears local endpoint");
expect(!controller.view().remote_endpoint, "reset clears remote endpoint");
expect(controller.view().packets_sent == 0 &&
       controller.view().packets_received == 0 &&
       controller.view().packets_lost == 0,
       "reset clears telemetry counters");
expect(controller.view().last_error == jojo::ErrorCode::none &&
       controller.view().last_error_detail.empty(),
       "reset clears error");
```

Then start a fresh host on the same controller to prove retry after reset.

- [ ] **Step 3: Add RED product-layer spoof test**

Use a third raw `UdpNetworkTransport` bound to loopback ephemeral port. After the host/client pair is connected, capture the host's pinned remote endpoint and send a syntactically valid ping from the attacker to the host local endpoint:

```cpp
auto attacker_result = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0));
expect(static_cast<bool>(attacker_result), "attacker transport binds");
if (attacker_result) {
    jojo::NetworkPacket spoof{};
    spoof.kind = jojo::NetworkPacketKind::ping;
    spoof.sequence = 900;
    spoof.timestamp_ms = 40;
    expect(static_cast<bool>(attacker_result.value.send_packet(
        *host.view().local_endpoint, spoof)), "attacker sends spoof ping");
    expect(static_cast<bool>(host.poll(40)), "host ignores spoof without error");
    expect(host.view().remote_endpoint == pinned,
           "spoof cannot replace product peer");
}
```

Continue silence past liveness timeout and assert the spoof did not prevent `reconnecting`.

- [ ] **Step 4: Run tests and verify RED where behavior is still missing**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: failures for unimplemented `send`, `disconnect`, `reset`, and/or lifecycle refresh semantics.

- [ ] **Step 5: Implement gameplay, disconnect, and reset delegation**

`send()`:

```cpp
if (view_.state != OnlineSessionState::connected || !session_) {
    return Result<void>::failure(ErrorCode::invalid_argument,
                                 "online gameplay send requires connection");
}
const auto result = session_->send(packet, now_ms);
if (!result) {
    if (result.error == ErrorCode::invalid_argument) return result;
    set_fault(result.error, result.detail);
    return result;
}
refresh_view();
return Result<void>::success();
```

`disconnect()` follows the same pattern but uses detail:

```cpp
"online disconnect requires connection"
```

On success, call `refresh_view()` so the product state immediately becomes `disconnected`.

`reset()`:

```cpp
void OnlineSessionController::reset() noexcept {
    session_.reset();
    view_ = OnlineSessionViewState{};
}
```

`poll()` must always call `refresh_view()` after a successful transport poll, even when zero gameplay packets were delivered, so heartbeat/reconnect/disconnect transitions and telemetry are visible to the product layer.

- [ ] **Step 6: Run Task 3 GREEN**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS for gameplay, reconnect/recovery, timeout, disconnect, reset, and spoof cases.

- [ ] **Step 7: Commit Task 3**

```bash
git add src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: complete direct online session lifecycle"
```

---

### Task 4: Operational fault contract, full regression, and package checkpoint

**Files:**
- Modify: `src/core/online_session.cpp`
- Modify: `tests/test_online_session.cpp`
- Modify after GREEN evidence: `PROJECT-STATE.md`
- Modify after GREEN evidence: `docs/NEXT-MILESTONES.md`
- Modify after GREEN evidence: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: completed controller from Tasks 1–3 and current GitHub Actions workflow.
- Produces: deterministic `faulted` behavior for operational failures, full Linux/Windows evidence, and documentation that points to the new evidence without changing truth status.

- [ ] **Step 1: Add RED operational-fault test using deterministic bind collision**

Bind one host to loopback ephemeral port, capture its actual local port, then start a second controller on that same explicit endpoint:

```cpp
jojo::OnlineSessionController first;
expect(static_cast<bool>(first.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
       "first host binds");
const auto occupied = *first.view().local_endpoint;

jojo::OnlineSessionController second;
const auto collision = second.host(occupied, fast_timing());
expect(!collision, "second host bind collision fails");
expect(collision.error == jojo::ErrorCode::io_error,
       "bind collision preserves transport io_error");
expect(second.view().state == jojo::OnlineSessionState::faulted,
       "operational bind failure faults controller");
expect(second.view().last_error == collision.error,
       "fault snapshot preserves error code");
expect(second.view().last_error_detail == collision.detail &&
       !second.view().last_error_detail.empty(),
       "fault snapshot preserves detail");

const auto retry_without_reset = second.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
expect(!retry_without_reset && retry_without_reset.error == jojo::ErrorCode::invalid_argument,
       "faulted controller requires reset before restart");
second.reset();
expect(static_cast<bool>(second.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
       "reset permits host after operational fault");
```

Also assert malformed endpoint parsing and join target port 0 remain validation errors and leave an inactive controller `inactive` so corrected input can be retried directly.

- [ ] **Step 2: Run and verify RED if fault persistence/restart gating is incomplete**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: FAIL until `set_fault` and faulted restart gating exactly match the contract.

- [ ] **Step 3: Implement the fault helper and preserve validation semantics**

Implement:

```cpp
void OnlineSessionController::set_fault(ErrorCode error, std::string detail) {
    view_.state = OnlineSessionState::faulted;
    view_.can_send_gameplay = false;
    view_.last_error = error;
    view_.last_error_detail = std::move(detail);
}
```

`host()` and `join()` must treat every non-`inactive` state, including `faulted`, as lifecycle misuse and return:

```cpp
Result<void>::failure(ErrorCode::invalid_argument,
                      "online session is already active")
```

without overwriting the preserved operational error snapshot.

Before `DirectUdpSession::bind`, clear no state except through the initial `inactive` precondition. A user validation rejection such as remote port 0 must leave the view at the untouched inactive baseline.

Operational failures from bind/connect/poll/gameplay send/disconnect must call `set_fault()` and return the original error/detail.

- [ ] **Step 4: Run focused controller GREEN**

Run:

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Run the existing transport contract locally**

Linux/macOS-style command:

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc \
  tests/test_network_transport.cpp src/core/network_protocol.cpp \
  -o network_transport_tests
./network_transport_tests
```

Expected: PASS. No changes to `tests/test_network_transport.cpp` or the standalone workflow command are required.

- [ ] **Step 6: Run the full local regression and readiness gates**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
```

Expected: all CTest targets, including `jojo_online_session_tests`, pass; both readiness scripts pass.

- [ ] **Step 7: Commit the completed implementation before CI**

```bash
git add CMakeLists.txt src/core/online_session.h src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: add R2.5 online session controller"
```

- [ ] **Step 8: Push the feature branch and require full GitHub Actions GREEN**

Push `feature/r2-5-online-controller`. The existing `.github/workflows/build.yml` must run unchanged because it already triggers on `feature/**`.

Require all of these on the same implementation HEAD:

- Portable core / Linux — Configure, Build, Production readiness gate, Test, R2.5 direct UDP transport contract: success.
- Windows x64 / MSVC 2022 — Configure, Build Release, Production readiness gate, Test Release, R2.5 direct UDP transport contract, Upload single executable: success.
- `jojo_online_session_tests` appears within normal CTest and passes on both OSes.
- Windows artifact remains named `JOJO-Recompiled-Windows-x64` and contains the single shipping `JOJO-Recompiled.exe` output.

Record the workflow run ID, implementation commit SHA, artifact ID, and SHA-256 digest before documentation updates.

- [ ] **Step 9: Update project truth documents only after GREEN evidence exists**

Update `PROJECT-STATE.md` with a concise new R2.5 controller checkpoint containing:

- branch `feature/r2-5-online-controller`;
- design path;
- implementation commit SHA;
- full Linux/Windows workflow run ID;
- artifact name/digest;
- implemented scope: direct IPv4 endpoint parser, host/join controller, stable product state/view snapshot, telemetry exposure, gameplay gating, reconnect/disconnect/reset mapping, validation-vs-operational error model, spoof-resistant peer presentation;
- explicit non-goals: no matchmaking/ranked/public rooms/invitations/accounts/NAT/relay/complete M9 UI;
- R2.5 remains `implemented-unverified`.

Update `docs/NEXT-MILESTONES.md` so the R2.5 paragraph states that the product-facing direct-session controller now exists and that the next user-facing increment is an in-game Online presentation/integration layer, still without production service claims.

Update only the R2.5 evidence field in `docs/architecture/PRODUCTION-READINESS.tsv`:

```tsv
R2.5	implemented-unverified	github-actions:run-<NEW_GREEN_RUN_ID>	none
```

Do not change any other workstream status or blocker.

- [ ] **Step 10: Commit the checkpoint documents**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "checkpoint: record R2.5 online controller evidence"
```

- [ ] **Step 11: Require one final full CI run on the documentation checkpoint HEAD**

Push the checkpoint commit and require the same Linux/Windows/readiness/CTest/standalone-R2.5/artifact gates to pass again. Confirm the final branch HEAD equals the tested checkpoint SHA and record the final artifact digest.

- [ ] **Step 12: Final truth check**

Verify all of the following before claiming package completion:

```text
feature/r2-5-online-controller HEAD == final GREEN checkpoint SHA
main unchanged from the package start
R2.5 status == implemented-unverified
R2.2/R2.4 blockers unchanged
R2.6 status unchanged
single executable policy unchanged
no commercial fingerprints/assets/data committed
```

Expected: every statement is true.
