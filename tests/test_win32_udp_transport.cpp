#include "core/network_protocol.h"
#include "platform/windows/udp_transport_win32.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; \
        ++failures; \
    } \
} while (false)

jojo::NetworkPacket make_input_packet() {
    jojo::NetworkPacket packet{};
    packet.kind = jojo::NetworkPacketKind::input;
    packet.sequence = 41;
    packet.ack = 17;
    packet.frame = 9001;
    packet.timestamp_ms = 123456789;
    packet.input.buttons = 0xA5A55A5Au;
    packet.input.axis_x = -1234;
    packet.input.axis_y = 2345;
    packet.payload = {1, 3, 3, 7};
    return packet;
}

void test_real_loopback_datagrams_round_trip_m8_packets() {
    auto first = jojo::Win32UdpTransport::bind({"127.0.0.1", 0});
    auto second = jojo::Win32UdpTransport::bind({"127.0.0.1", 0});
    CHECK(first);
    CHECK(second);
    if (!first || !second) return;

    CHECK(first.value.local_port() != 0);
    CHECK(second.value.local_port() != 0);
    CHECK(first.value.local_port() != second.value.local_port());

    const auto packet = make_input_packet();
    const auto encoded = jojo::serialize_network_packet(packet);
    CHECK(encoded);
    if (!encoded) return;

    const jojo::UdpEndpoint second_endpoint{"127.0.0.1", second.value.local_port()};
    CHECK(first.value.send_to(second_endpoint, encoded.value));

    const auto received = second.value.receive(1000);
    CHECK(received);
    if (!received) return;

    CHECK(received.value.remote.host == "127.0.0.1");
    CHECK(received.value.remote.port == first.value.local_port());
    CHECK(received.value.bytes == encoded.value);

    const auto decoded = jojo::parse_network_packet(received.value.bytes);
    CHECK(decoded);
    if (decoded) CHECK(decoded.value == packet);

    jojo::NetworkPacket pong{};
    pong.kind = jojo::NetworkPacketKind::pong;
    pong.sequence = 42;
    pong.ack = packet.sequence;
    pong.timestamp_ms = packet.timestamp_ms + 5;
    const auto pong_bytes = jojo::serialize_network_packet(pong);
    CHECK(pong_bytes);
    if (!pong_bytes) return;

    CHECK(second.value.send_to(received.value.remote, pong_bytes.value));
    const auto response = first.value.receive(1000);
    CHECK(response);
    if (!response) return;

    const auto parsed_response = jojo::parse_network_packet(response.value.bytes);
    CHECK(parsed_response);
    if (parsed_response) CHECK(parsed_response.value == pong);
}

void test_timeout_is_explicit_and_nonfatal() {
    auto transport = jojo::Win32UdpTransport::bind({"127.0.0.1", 0});
    CHECK(transport);
    if (!transport) return;

    const auto timed_out = transport.value.receive(20);
    CHECK(!timed_out);
    CHECK(timed_out.error == jojo::ErrorCode::io_error);
    CHECK(timed_out.detail.find("timed out") != std::string::npos);
    CHECK(transport.value.local_port() != 0);
}

void test_invalid_endpoints_and_empty_payload_are_rejected() {
    CHECK(!jojo::Win32UdpTransport::bind({"", 0}));
    CHECK(!jojo::Win32UdpTransport::bind({"not-an-ip", 0}));

    auto transport = jojo::Win32UdpTransport::bind({"127.0.0.1", 0});
    CHECK(transport);
    if (!transport) return;

    CHECK(!transport.value.send_to({"127.0.0.1", 0}, std::vector<std::uint8_t>{1}));
    CHECK(!transport.value.send_to({"not-an-ip", 12345}, std::vector<std::uint8_t>{1}));
    CHECK(!transport.value.send_to({"127.0.0.1", transport.value.local_port()}, {}));
}

} // namespace

int main() {
    test_real_loopback_datagrams_round_trip_m8_packets();
    test_timeout_is_explicit_and_nonfatal();
    test_invalid_endpoints_and_empty_payload_are_rejected();

    if (failures != 0) {
        std::cerr << failures << " WinSock UDP transport assertion(s) failed\n";
        return 1;
    }
    std::cout << "all WinSock UDP transport assertions passed\n";
    return 0;
}
