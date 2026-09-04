#include "core/online_session.h"

#include <array>
#include <cctype>

namespace jojo {
namespace {

Result<unsigned> parse_decimal(std::string_view text, unsigned maximum,
                               const char* detail) {
    if (text.empty()) {
        return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
    }

    unsigned value = 0u;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
        }
        const unsigned digit = static_cast<unsigned>(ch - '0');
        if (value > (maximum - digit) / 10u) {
            return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
        }
        value = value * 10u + digit;
    }
    return Result<unsigned>::success(value);
}

} // namespace

Result<NetworkEndpoint> parse_direct_endpoint(std::string_view text) {
    for (const unsigned char ch : text) {
        if (std::isspace(ch) != 0) {
            return Result<NetworkEndpoint>::failure(
                ErrorCode::invalid_argument,
                "direct endpoint must use A.B.C.D:PORT");
        }
    }

    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0u ||
        colon + 1u >= text.size() ||
        text.find(':', colon + 1u) != std::string_view::npos) {
        return Result<NetworkEndpoint>::failure(
            ErrorCode::invalid_argument,
            "direct endpoint must use A.B.C.D:PORT");
    }

    std::array<std::uint8_t, 4> octets{};
    const std::string_view address = text.substr(0u, colon);
    std::size_t begin = 0u;
    for (std::size_t index = 0u; index < octets.size(); ++index) {
        const auto dot = address.find('.', begin);
        const bool last = index + 1u == octets.size();
        if ((last && dot != std::string_view::npos) ||
            (!last && dot == std::string_view::npos)) {
            return Result<NetworkEndpoint>::failure(
                ErrorCode::invalid_argument,
                "direct endpoint must use A.B.C.D:PORT");
        }

        const auto end = last ? address.size() : dot;
        const auto value = parse_decimal(
            address.substr(begin, end - begin), 255u,
            "direct endpoint IPv4 octet is invalid");
        if (!value) {
            return Result<NetworkEndpoint>::failure(value.error, value.detail);
        }
        octets[index] = static_cast<std::uint8_t>(value.value);
        begin = end + 1u;
    }

    const auto port = parse_decimal(
        text.substr(colon + 1u), 65535u,
        "direct endpoint port is invalid");
    if (!port || port.value == 0u) {
        return Result<NetworkEndpoint>::failure(
            ErrorCode::invalid_argument,
            "direct endpoint port is invalid");
    }

    return Result<NetworkEndpoint>::success(
        NetworkEndpoint{octets, static_cast<std::uint16_t>(port.value)});
}

std::string format_direct_endpoint(NetworkEndpoint endpoint) {
    return std::to_string(endpoint.ipv4[0]) + "." +
           std::to_string(endpoint.ipv4[1]) + "." +
           std::to_string(endpoint.ipv4[2]) + "." +
           std::to_string(endpoint.ipv4[3]) + ":" +
           std::to_string(endpoint.port);
}

} // namespace jojo
