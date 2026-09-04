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
