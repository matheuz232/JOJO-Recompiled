#include "core/online_menu.h"

#include <iostream>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
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
    return failures == 0 ? 0 : 1;
}
