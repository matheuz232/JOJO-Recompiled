#pragma once

#ifdef _WIN32

#include "core/result.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace jojo {

struct UdpEndpoint {
    std::string host;
    std::uint16_t port{};

    friend bool operator==(const UdpEndpoint&, const UdpEndpoint&) = default;
};

struct UdpDatagram {
    UdpEndpoint remote;
    std::vector<std::uint8_t> bytes;
};

class Win32UdpTransport {
public:
    Win32UdpTransport() noexcept = default;
    ~Win32UdpTransport();

    Win32UdpTransport(const Win32UdpTransport&) = delete;
    Win32UdpTransport& operator=(const Win32UdpTransport&) = delete;

    Win32UdpTransport(Win32UdpTransport&& other) noexcept;
    Win32UdpTransport& operator=(Win32UdpTransport&& other) noexcept;

    [[nodiscard]] static Result<Win32UdpTransport> bind(const UdpEndpoint& local);

    [[nodiscard]] Result<void> send_to(
        const UdpEndpoint& remote,
        std::span<const std::uint8_t> bytes);
    [[nodiscard]] Result<UdpDatagram> receive(std::uint32_t timeout_ms);

    [[nodiscard]] std::uint16_t local_port() const noexcept { return local_port_; }
    [[nodiscard]] bool valid() const noexcept;

private:
    explicit Win32UdpTransport(
        std::uintptr_t socket_handle,
        int family,
        std::uint16_t local_port) noexcept;

    void reset() noexcept;

    std::uintptr_t socket_handle_{~std::uintptr_t{0}};
    int family_{};
    std::uint16_t local_port_{};
    bool winsock_started_{};
};

} // namespace jojo

#endif
