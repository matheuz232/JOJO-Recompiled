#include "core/online_menu.h"

#include <utility>

namespace jojo {

void OnlineMenuSession::set_join_endpoint(std::string text) {
    view_.join_endpoint_text = std::move(text);
    view_.validation_error.clear();
}

Result<void> OnlineMenuSession::start_host(NetworkEndpoint local,
                                           DirectSessionTiming timing) {
    if (view_.screen != OnlineMenuScreen::home) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online menu start requires home screen");
    }

    const auto result = controller_.host(local, timing);
    refresh_view();
    return result;
}

Result<void> OnlineMenuSession::start_join(NetworkEndpoint local,
                                           DirectSessionTiming timing,
                                           std::uint64_t now_ms) {
    if (view_.screen != OnlineMenuScreen::home) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online menu start requires home screen");
    }

    const auto remote = parse_direct_endpoint(view_.join_endpoint_text);
    if (!remote) {
        view_.validation_error = remote.detail;
        return Result<void>::failure(remote.error, remote.detail);
    }

    view_.validation_error.clear();
    const auto result = controller_.join(local, remote.value, timing, now_ms);
    refresh_view();
    return result;
}

Result<std::vector<NetworkPacket>> OnlineMenuSession::tick(std::uint64_t now_ms) {
    const auto result = controller_.poll(now_ms);
    refresh_view();
    return result;
}

Result<void> OnlineMenuSession::disconnect(std::uint64_t now_ms) {
    const auto result = controller_.disconnect(now_ms);
    refresh_view();
    return result;
}

void OnlineMenuSession::return_home() noexcept {
    controller_.reset();
    refresh_view();
}

void OnlineMenuSession::refresh_view() noexcept {
    const auto& session = controller_.view();
    switch (session.state) {
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

    view_.role = session.role;
    view_.local_endpoint = session.local_endpoint;
    view_.remote_endpoint = session.remote_endpoint;
    view_.rtt_ms = session.rtt_ms;
    view_.jitter_ms = session.jitter_ms;
    view_.packet_loss_percent = session.packet_loss_percent;
    view_.packets_sent = session.packets_sent;
    view_.packets_received = session.packets_received;
    view_.packets_lost = session.packets_lost;
    view_.session_error = session.last_error;
    view_.session_error_detail = session.last_error_detail;

    view_.can_host = view_.screen == OnlineMenuScreen::home;
    view_.can_join = view_.screen == OnlineMenuScreen::home;
    view_.can_disconnect = view_.screen == OnlineMenuScreen::connected;
    view_.can_return_home = view_.screen == OnlineMenuScreen::hosting ||
                            view_.screen == OnlineMenuScreen::joining ||
                            view_.screen == OnlineMenuScreen::reconnecting ||
                            view_.screen == OnlineMenuScreen::disconnected ||
                            view_.screen == OnlineMenuScreen::faulted;
    view_.can_start_gameplay = session.can_send_gameplay;
}

} // namespace jojo
