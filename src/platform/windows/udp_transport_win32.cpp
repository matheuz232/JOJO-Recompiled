#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform/windows/udp_transport_win32.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace jojo {
namespace {

constexpr std::uintptr_t invalid_socket_handle = ~std::uintptr_t{0};
constexpr std::size_t max_udp_datagram_size = 65535u;

struct ResolvedAddress {
    sockaddr_storage storage{};
    int length{};
    int family{};
};

SOCKET as_socket(std::uintptr_t handle) noexcept {
    return static_cast<SOCKET>(handle);
}

std::string socket_error_detail(const char* operation, int code) {
    return std::string(operation) + " failed with WinSock error " + std::to_string(code);
}

Result<ResolvedAddress> resolve_numeric_endpoint(
    const UdpEndpoint& endpoint,
    int family,
    bool allow_zero_port) {
    if (endpoint.host.empty()) {
        return Result<ResolvedAddress>::failure(
            ErrorCode::invalid_argument,
            "UDP endpoint host must not be empty");
    }
    if (!allow_zero_port && endpoint.port == 0u) {
        return Result<ResolvedAddress>::failure(
            ErrorCode::invalid_argument,
            "UDP remote port must not be zero");
    }

    addrinfo hints{};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    addrinfo* addresses = nullptr;
    const auto service = std::to_string(endpoint.port);
    const int result = getaddrinfo(endpoint.host.c_str(), service.c_str(), &hints, &addresses);
    if (result != 0 || addresses == nullptr) {
        if (addresses != nullptr) freeaddrinfo(addresses);
        return Result<ResolvedAddress>::failure(
            ErrorCode::invalid_argument,
            "UDP endpoint must be a numeric IPv4 or IPv6 address");
    }

    ResolvedAddress resolved{};
    if (addresses->ai_addrlen > sizeof(resolved.storage)) {
        freeaddrinfo(addresses);
        return Result<ResolvedAddress>::failure(
            ErrorCode::invalid_argument,
            "UDP endpoint address is too large for sockaddr_storage");
    }

    std::memcpy(&resolved.storage, addresses->ai_addr, addresses->ai_addrlen);
    resolved.length = static_cast<int>(addresses->ai_addrlen);
    resolved.family = addresses->ai_family;
    freeaddrinfo(addresses);
    return Result<ResolvedAddress>::success(resolved);
}

std::uint16_t port_from_sockaddr(const sockaddr_storage& storage) noexcept {
    if (storage.ss_family == AF_INET) {
        const auto* address = reinterpret_cast<const sockaddr_in*>(&storage);
        return ntohs(address->sin_port);
    }
    if (storage.ss_family == AF_INET6) {
        const auto* address = reinterpret_cast<const sockaddr_in6*>(&storage);
        return ntohs(address->sin6_port);
    }
    return 0u;
}

Result<UdpEndpoint> endpoint_from_sockaddr(
    const sockaddr_storage& storage,
    int length) {
    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    const int result = getnameinfo(
        reinterpret_cast<const sockaddr*>(&storage),
        length,
        host.data(),
        static_cast<DWORD>(host.size()),
        service.data(),
        static_cast<DWORD>(service.size()),
        NI_NUMERICHOST | NI_NUMERICSERV);
    if (result != 0) {
        return Result<UdpEndpoint>::failure(
            ErrorCode::io_error,
            "failed to format UDP sender endpoint");
    }

    unsigned long parsed_port = 0;
    try {
        parsed_port = std::stoul(service.data());
    } catch (...) {
        return Result<UdpEndpoint>::failure(
            ErrorCode::io_error,
            "received UDP sender port is invalid");
    }
    if (parsed_port > std::numeric_limits<std::uint16_t>::max()) {
        return Result<UdpEndpoint>::failure(
            ErrorCode::io_error,
            "received UDP sender port is outside uint16 range");
    }

    return Result<UdpEndpoint>::success(
        UdpEndpoint{host.data(), static_cast<std::uint16_t>(parsed_port)});
}

} // namespace

Win32UdpTransport::Win32UdpTransport(
    std::uintptr_t socket_handle,
    int family,
    std::uint16_t local_port) noexcept
    : socket_handle_(socket_handle),
      family_(family),
      local_port_(local_port),
      winsock_started_(true) {}

Win32UdpTransport::~Win32UdpTransport() {
    reset();
}

Win32UdpTransport::Win32UdpTransport(Win32UdpTransport&& other) noexcept
    : socket_handle_(std::exchange(other.socket_handle_, invalid_socket_handle)),
      family_(std::exchange(other.family_, 0)),
      local_port_(std::exchange(other.local_port_, 0u)),
      winsock_started_(std::exchange(other.winsock_started_, false)) {}

Win32UdpTransport& Win32UdpTransport::operator=(Win32UdpTransport&& other) noexcept {
    if (this == &other) return *this;
    reset();
    socket_handle_ = std::exchange(other.socket_handle_, invalid_socket_handle);
    family_ = std::exchange(other.family_, 0);
    local_port_ = std::exchange(other.local_port_, 0u);
    winsock_started_ = std::exchange(other.winsock_started_, false);
    return *this;
}

bool Win32UdpTransport::valid() const noexcept {
    return socket_handle_ != invalid_socket_handle && winsock_started_;
}

void Win32UdpTransport::reset() noexcept {
    if (socket_handle_ != invalid_socket_handle) {
        closesocket(as_socket(socket_handle_));
        socket_handle_ = invalid_socket_handle;
    }
    family_ = 0;
    local_port_ = 0u;
    if (winsock_started_) {
        WSACleanup();
        winsock_started_ = false;
    }
}

Result<Win32UdpTransport> Win32UdpTransport::bind(const UdpEndpoint& local) {
    WSADATA data{};
    const int startup = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup != 0) {
        return Result<Win32UdpTransport>::failure(
            ErrorCode::io_error,
            socket_error_detail("WSAStartup", startup));
    }

    const auto resolved = resolve_numeric_endpoint(local, AF_UNSPEC, true);
    if (!resolved) {
        WSACleanup();
        return Result<Win32UdpTransport>::failure(resolved.error, resolved.detail);
    }

    const SOCKET socket_handle = socket(
        resolved.value.family,
        SOCK_DGRAM,
        IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        const int error = WSAGetLastError();
        WSACleanup();
        return Result<Win32UdpTransport>::failure(
            ErrorCode::io_error,
            socket_error_detail("socket", error));
    }

    if (::bind(
            socket_handle,
            reinterpret_cast<const sockaddr*>(&resolved.value.storage),
            resolved.value.length) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        closesocket(socket_handle);
        WSACleanup();
        return Result<Win32UdpTransport>::failure(
            ErrorCode::io_error,
            socket_error_detail("bind", error));
    }

    sockaddr_storage bound{};
    int bound_length = sizeof(bound);
    if (getsockname(
            socket_handle,
            reinterpret_cast<sockaddr*>(&bound),
            &bound_length) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        closesocket(socket_handle);
        WSACleanup();
        return Result<Win32UdpTransport>::failure(
            ErrorCode::io_error,
            socket_error_detail("getsockname", error));
    }

    const auto port = port_from_sockaddr(bound);
    if (port == 0u) {
        closesocket(socket_handle);
        WSACleanup();
        return Result<Win32UdpTransport>::failure(
            ErrorCode::io_error,
            "WinSock returned an invalid zero local UDP port");
    }

    return Result<Win32UdpTransport>::success(
        Win32UdpTransport{
            static_cast<std::uintptr_t>(socket_handle),
            resolved.value.family,
            port});
}

Result<void> Win32UdpTransport::send_to(
    const UdpEndpoint& remote,
    std::span<const std::uint8_t> bytes) {
    if (!valid()) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "UDP transport is not open");
    }
    if (bytes.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "UDP datagram must not be empty");
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "UDP datagram exceeds WinSock send length range");
    }

    const auto resolved = resolve_numeric_endpoint(remote, family_, false);
    if (!resolved) return Result<void>::failure(resolved.error, resolved.detail);

    const int sent = sendto(
        as_socket(socket_handle_),
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        0,
        reinterpret_cast<const sockaddr*>(&resolved.value.storage),
        resolved.value.length);
    if (sent == SOCKET_ERROR) {
        return Result<void>::failure(
            ErrorCode::io_error,
            socket_error_detail("sendto", WSAGetLastError()));
    }
    if (sent != static_cast<int>(bytes.size())) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "WinSock reported a partial UDP datagram send");
    }
    return Result<void>::success();
}

Result<UdpDatagram> Win32UdpTransport::receive(std::uint32_t timeout_ms) {
    if (!valid()) {
        return Result<UdpDatagram>::failure(
            ErrorCode::backend_unavailable,
            "UDP transport is not open");
    }

    fd_set read_set{};
    FD_ZERO(&read_set);
    FD_SET(as_socket(socket_handle_), &read_set);

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000u);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);

    const int selected = select(0, &read_set, nullptr, nullptr, &timeout);
    if (selected == SOCKET_ERROR) {
        return Result<UdpDatagram>::failure(
            ErrorCode::io_error,
            socket_error_detail("select", WSAGetLastError()));
    }
    if (selected == 0) {
        return Result<UdpDatagram>::failure(
            ErrorCode::io_error,
            "UDP receive timed out");
    }

    std::vector<std::uint8_t> bytes(max_udp_datagram_size);
    sockaddr_storage sender{};
    int sender_length = sizeof(sender);
    const int received = recvfrom(
        as_socket(socket_handle_),
        reinterpret_cast<char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        0,
        reinterpret_cast<sockaddr*>(&sender),
        &sender_length);
    if (received == SOCKET_ERROR) {
        return Result<UdpDatagram>::failure(
            ErrorCode::io_error,
            socket_error_detail("recvfrom", WSAGetLastError()));
    }

    bytes.resize(static_cast<std::size_t>(received));
    const auto remote = endpoint_from_sockaddr(sender, sender_length);
    if (!remote) {
        return Result<UdpDatagram>::failure(remote.error, remote.detail);
    }

    UdpDatagram datagram{};
    datagram.remote = remote.value;
    datagram.bytes = std::move(bytes);
    return Result<UdpDatagram>::success(std::move(datagram));
}

} // namespace jojo

#endif
