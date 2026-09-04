#include "core/online_session.h"

#include <cstdint>
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
        const auto message = "reject endpoint: " + bad;
        expect(!result, message.c_str());
        if (!result) {
            expect(result.error == jojo::ErrorCode::invalid_argument,
                   "malformed endpoint uses invalid_argument");
            expect(!result.detail.empty(), "malformed endpoint has detail");
        }
    }
}

void test_host_join_view() {
    jojo::OnlineSessionController idle;
    const auto idle_poll = idle.poll(0);
    expect(!idle_poll && idle_poll.error == jojo::ErrorCode::invalid_argument,
           "poll without active session is validation failure");
    expect(idle.view().state == jojo::OnlineSessionState::inactive,
           "inactive poll does not fault controller");

    jojo::OnlineSessionController host;
    const auto host_started = host.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(static_cast<bool>(host_started), "host binds");
    expect(host.view().state == jojo::OnlineSessionState::waiting_for_peer,
           "host waits for peer");
    expect(host.view().role == jojo::DirectSessionRole::host, "host role exposed");
    expect(host.view().local_endpoint && host.view().local_endpoint->port != 0,
           "ephemeral host port resolved");
    expect(!host.view().remote_endpoint, "host has no peer before hello");
    expect(!host.view().can_send_gameplay, "host cannot send while waiting");

    const auto host_state = host.view().state;
    const auto duplicate_host = host.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(!duplicate_host && duplicate_host.error == jojo::ErrorCode::invalid_argument,
           "second host start is validation failure");
    expect(host.view().state == host_state, "second host start preserves lifecycle");

    jojo::OnlineSessionController client;
    const auto joined = client.join(jojo::NetworkEndpoint::loopback(0),
                                    *host.view().local_endpoint,
                                    fast_timing(), 0);
    expect(static_cast<bool>(joined), "client join starts");
    expect(client.view().state == jojo::OnlineSessionState::connecting,
           "client exposes connecting");
    expect(client.view().role == jojo::DirectSessionRole::client,
           "client role exposed");
    expect(client.view().remote_endpoint == host.view().local_endpoint,
           "client target exposed immediately");

    const auto client_state = client.view().state;
    const auto duplicate_join = client.join(jojo::NetworkEndpoint::loopback(0),
                                            *host.view().local_endpoint,
                                            fast_timing(), 0);
    expect(!duplicate_join && duplicate_join.error == jojo::ErrorCode::invalid_argument,
           "second join is validation failure");
    expect(client.view().state == client_state, "second join preserves lifecycle");

    expect(drive_connected(host, client), "controllers connect through polling");
    expect(host.view().state == jojo::OnlineSessionState::connected,
           "host reaches connected");
    expect(client.view().state == jojo::OnlineSessionState::connected,
           "client reaches connected");
    expect(host.view().remote_endpoint == client.view().local_endpoint,
           "host exposes pinned client endpoint");
    expect(host.view().can_send_gameplay && client.view().can_send_gameplay,
           "gameplay gate opens when connected");
}
} // namespace

int main() {
    test_endpoint_parser();
    test_host_join_view();
    return failures == 0 ? 0 : 1;
}
