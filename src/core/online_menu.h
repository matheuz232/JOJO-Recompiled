#pragma once

#include "core/online_session.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jojo {

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

} // namespace jojo
