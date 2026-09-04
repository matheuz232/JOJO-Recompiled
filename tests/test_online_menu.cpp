#include "core/online_menu.h"

#include <cstdint>
#include <iostream>

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

void test_initial_view() {
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
}

void test_join_form_validation() {
    jojo::OnlineMenuSession menu;
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
}

void test_host_mapping_and_duplicate_start() {
    jojo::OnlineMenuSession menu;
    const auto hosted = menu.start_host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(static_cast<bool>(hosted), "menu host starts");
    expect(menu.view().screen == jojo::OnlineMenuScreen::hosting,
           "waiting controller maps to hosting");
    expect(menu.view().role == jojo::DirectSessionRole::host, "host role mirrored");
    expect(menu.view().local_endpoint && menu.view().local_endpoint->port != 0,
           "host endpoint mirrored");
    expect(!menu.view().can_host && !menu.view().can_join,
           "hosting blocks new starts");
    expect(menu.view().can_return_home, "hosting permits return home");

    const auto duplicate = menu.start_host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(!duplicate && duplicate.error == jojo::ErrorCode::invalid_argument,
           "duplicate host is validation failure");
    expect(menu.view().screen == jojo::OnlineMenuScreen::hosting,
           "duplicate host preserves lifecycle");
}

void test_join_mapping_and_fault_preservation() {
    jojo::OnlineMenuSession host;
    expect(static_cast<bool>(host.start_host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
           "fault fixture host starts");
    if (!host.view().local_endpoint) return;

    jojo::OnlineMenuSession faulted;
    const auto collision = faulted.start_host(*host.view().local_endpoint, fast_timing());
    expect(!collision && collision.error == jojo::ErrorCode::io_error,
           "operational host failure is exposed");
    expect(faulted.view().screen == jojo::OnlineMenuScreen::faulted,
           "operational host failure maps to faulted");
    expect(faulted.view().session_error == collision.error &&
               faulted.view().session_error_detail == collision.detail &&
               !faulted.view().session_error_detail.empty(),
           "operational fault evidence is mirrored");
    expect(faulted.view().can_return_home, "faulted menu permits return home");

    const auto duplicate_host = faulted.start_host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(!duplicate_host && duplicate_host.error == jojo::ErrorCode::invalid_argument,
           "faulted duplicate host is validation failure");
    expect(faulted.view().session_error == collision.error &&
               faulted.view().session_error_detail == collision.detail,
           "duplicate host preserves operational fault evidence");

    faulted.set_join_endpoint("127.0.0.1:34567");
    const auto duplicate_join = faulted.start_join(jojo::NetworkEndpoint::loopback(0), fast_timing(), 0);
    expect(!duplicate_join && duplicate_join.error == jojo::ErrorCode::invalid_argument,
           "faulted duplicate join is validation failure");
    expect(faulted.view().session_error == collision.error &&
               faulted.view().session_error_detail == collision.detail,
           "duplicate join preserves operational fault evidence");
}
} // namespace

int main() {
    test_initial_view();
    test_join_form_validation();
    test_host_mapping_and_duplicate_start();
    test_join_mapping_and_fault_preservation();
    return failures == 0 ? 0 : 1;
}
