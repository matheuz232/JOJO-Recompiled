#include "core/online_menu.h"

#include <iostream>

namespace jojo {
struct OnlineMenuSessionTestAccess {
    static OnlineSessionController& controller(OnlineMenuSession& menu) {
        return menu.controller_;
    }
};
} // namespace jojo

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

void test_join_form_validation() {
    jojo::OnlineMenuSession menu;
    menu.set_join_endpoint("bad-endpoint");
    expect(menu.view().join_endpoint_text == "bad-endpoint", "endpoint text stored");

    const auto invalid = menu.start_join(jojo::NetworkEndpoint::loopback(0),
                                         fast_timing(), 0);
    expect(!invalid && invalid.error == jojo::ErrorCode::invalid_argument,
           "malformed join is validation failure");
    expect(menu.view().screen == jojo::OnlineMenuScreen::home,
           "malformed join remains home");
    expect(!menu.view().validation_error.empty(), "validation detail exposed");
    expect(menu.view().session_error == jojo::ErrorCode::none,
           "form validation does not become session fault");

    menu.set_join_endpoint("127.0.0.1:34567");
    expect(menu.view().validation_error.empty(), "editing clears form error");
}

void test_host_join_mapping_and_delegation() {
    jojo::OnlineMenuSession host;
    const auto hosted = host.start_host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(static_cast<bool>(hosted), "menu host delegates to controller");
    expect(host.view().screen == jojo::OnlineMenuScreen::hosting,
           "waiting controller maps to hosting screen");
    expect(host.view().role == jojo::DirectSessionRole::host, "host role mirrored");
    expect(host.view().local_endpoint && host.view().local_endpoint->port != 0,
           "host local endpoint mirrored");
    expect(!host.view().can_host && !host.view().can_join,
           "hosting blocks duplicate start actions");
    expect(host.view().can_return_home, "hosting can return home");

    const auto duplicate_host = host.start_host(jojo::NetworkEndpoint::loopback(0),
                                                 fast_timing());
    expect(!duplicate_host && duplicate_host.error == jojo::ErrorCode::invalid_argument,
           "duplicate host is validation failure");
    expect(host.view().screen == jojo::OnlineMenuScreen::hosting,
           "duplicate host preserves lifecycle view");

    jojo::OnlineMenuSession client;
    client.set_join_endpoint(jojo::format_direct_endpoint(*host.view().local_endpoint));
    const auto joined = client.start_join(jojo::NetworkEndpoint::loopback(0), fast_timing(), 0);
    expect(static_cast<bool>(joined), "menu join delegates to controller");
    expect(client.view().screen == jojo::OnlineMenuScreen::joining,
           "connecting controller maps to joining screen");
    expect(client.view().role == jojo::DirectSessionRole::client, "client role mirrored");
    expect(client.view().remote_endpoint == host.view().local_endpoint,
           "client remote endpoint mirrored");
    expect(client.view().can_return_home, "joining can return home");

    const auto duplicate_join = client.start_join(jojo::NetworkEndpoint::loopback(0),
                                                   fast_timing(), 0);
    expect(!duplicate_join && duplicate_join.error == jojo::ErrorCode::invalid_argument,
           "duplicate join is validation failure");
    expect(client.view().screen == jojo::OnlineMenuScreen::joining,
           "duplicate join preserves lifecycle view");

    for (std::uint64_t now = 1; now <= 20; ++now) {
        expect(static_cast<bool>(host.tick(now)), "host menu tick succeeds");
        expect(static_cast<bool>(client.tick(now)), "client menu tick succeeds");
        if (host.view().screen == jojo::OnlineMenuScreen::connected &&
            client.view().screen == jojo::OnlineMenuScreen::connected) break;
    }
    expect(host.view().screen == jojo::OnlineMenuScreen::connected &&
               client.view().screen == jojo::OnlineMenuScreen::connected,
           "menus map controller connection to connected screen");
    expect(host.view().can_disconnect && client.view().can_disconnect,
           "connected enables disconnect");
    expect(host.view().can_start_gameplay && client.view().can_start_gameplay,
           "connected mirrors gameplay gate");
    expect(!host.view().can_return_home, "connected has no return-home transition");

    expect(static_cast<bool>(host.tick(60)), "host advances liveness to reconnect");
    if (host.view().screen != jojo::OnlineMenuScreen::reconnecting) {
        expect(static_cast<bool>(host.tick(100)), "host enters reconnecting");
    }
    expect(host.view().screen == jojo::OnlineMenuScreen::reconnecting,
           "reconnecting controller maps to reconnecting screen");
    expect(!host.view().can_start_gameplay && host.view().can_return_home,
           "reconnecting updates gameplay and return-home permissions");

    const auto disconnected = client.disconnect(101);
    expect(static_cast<bool>(disconnected), "menu disconnect delegates to controller");
    expect(client.view().screen == jojo::OnlineMenuScreen::disconnected,
           "disconnect maps to disconnected screen");
    expect(client.view().can_return_home, "disconnected can return home");
    client.return_home();
    expect(client.view().screen == jojo::OnlineMenuScreen::home,
           "return home resets controller lifecycle");
}

void test_fault_and_misuse_preserve_fault() {
    jojo::OnlineMenuSession first;
    expect(static_cast<bool>(first.start_host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
           "first menu host starts");
    if (!first.view().local_endpoint) return;

    jojo::OnlineMenuSession faulted;
    const auto collision = faulted.start_host(*first.view().local_endpoint, fast_timing());
    expect(!collision && collision.error == jojo::ErrorCode::io_error,
           "host bind collision reports operational failure");
    expect(faulted.view().screen == jojo::OnlineMenuScreen::faulted,
           "operational failure maps to faulted screen");
    expect(faulted.view().session_error == collision.error &&
               faulted.view().session_error_detail == collision.detail,
           "fault details are mirrored");

    const auto preserved_error = faulted.view().session_error;
    const auto preserved_detail = faulted.view().session_error_detail;
    const auto duplicate_host = faulted.start_host(jojo::NetworkEndpoint::loopback(0),
                                                   fast_timing());
    expect(!duplicate_host && duplicate_host.error == jojo::ErrorCode::invalid_argument,
           "misused host from fault is validation failure");
    expect(faulted.view().session_error == preserved_error &&
               faulted.view().session_error_detail == preserved_detail,
           "misused host does not overwrite operational fault");
    faulted.set_join_endpoint("127.0.0.1:34567");
    const auto duplicate_join = faulted.start_join(jojo::NetworkEndpoint::loopback(0),
                                                    fast_timing(), 0);
    expect(!duplicate_join && duplicate_join.error == jojo::ErrorCode::invalid_argument,
           "misused join from fault is validation failure");
    expect(faulted.view().session_error == preserved_error &&
               faulted.view().session_error_detail == preserved_detail,
           "misused join does not overwrite operational fault");
}
} // namespace

int main() {
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
    test_join_form_validation();
    test_host_join_mapping_and_delegation();
    test_fault_and_misuse_preserve_fault();
    return failures == 0 ? 0 : 1;
}
