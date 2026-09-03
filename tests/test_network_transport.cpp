#if __has_include("core/network_transport.h")
#include "core/network_transport.h"
#define JOJO_HAS_NETWORK_TRANSPORT 1
#else
#define JOJO_HAS_NETWORK_TRANSPORT 0
#endif

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

#if JOJO_HAS_NETWORK_TRANSPORT

static jojo::NetworkPacket make_packet(jojo::NetworkPacketKind kind,
                                       std::uint32_t sequence) {
    jojo::NetworkPacket packet{};
    packet.kind = kind;
    packet.sequence = sequence;
    packet.frame = 77u;
    packet.timestamp_ms = 1234u;
    packet.input.buttons = 0x1234u;
    packet.input.axis_x = -321;
    packet.input.axis_y = 456;
    packet.payload = {0x4Au, 0x4Fu, 0x4Au, 0x4Fu};
    return packet;
}

static void test_loopback_udp_is_nonblocking_and_preserves_datagrams() {
    auto a = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0u));
    auto b = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0u));
    CHECK(a && b);
    if (!a || !b) return;

    const auto a_local = a.value.local_endpoint();
    const auto b_local = b.value.local_endpoint();
    CHECK(a_local.port != 0u);
    CHECK(b_local.port != 0u);
    CHECK(a_local != b_local);

    const auto empty = b.value.receive_datagram();
    CHECK(empty);
    if (empty) CHECK(!empty.value.has_value());

    const std::vector<std::uint8_t> bytes{0x10u, 0x20u, 0x30u, 0x40u};
    CHECK(a.value.send_datagram(b_local, bytes));
    const auto received = b.value.receive_datagram();
    CHECK(received);
    if (received && received.value) {
        CHECK(received.value->source == a_local);
        CHECK(received.value->bytes == bytes);
    }
}

static void test_transport_bounds_datagrams_and_parses_network_packets() {
    auto sender = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0u));
    auto receiver = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0u));
    CHECK(sender && receiver);
    if (!sender || !receiver) return;

    std::vector<std::uint8_t> oversized(jojo::kNetworkTransportMaxDatagram + 1u, 0x5Au);
    CHECK(!sender.value.send_datagram(receiver.value.local_endpoint(), oversized));

    const auto packet = make_packet(jojo::NetworkPacketKind::session_hello, 41u);
    CHECK(sender.value.send_packet(receiver.value.local_endpoint(), packet));
    const auto received = receiver.value.receive_packet();
    CHECK(received);
    if (received && received.value) {
        CHECK(received.value->source == sender.value.local_endpoint());
        CHECK(received.value->packet == packet);
    }

    const std::vector<std::uint8_t> malformed{0x00u, 0x01u, 0x02u};
    CHECK(sender.value.send_datagram(receiver.value.local_endpoint(), malformed));
    CHECK(!receiver.value.receive_packet());
}

static void test_direct_session_handshake_and_gameplay_packet_round_trip() {
    auto host = jojo::DirectUdpSession::bind(
        jojo::DirectSessionRole::host, jojo::NetworkEndpoint::loopback(0u));
    auto client = jojo::DirectUdpSession::bind(
        jojo::DirectSessionRole::client, jojo::NetworkEndpoint::loopback(0u));
    CHECK(host && client);
    if (!host || !client) return;

    CHECK(host.value.state() == jojo::DirectSessionState::idle);
    CHECK(client.value.state() == jojo::DirectSessionState::idle);
    CHECK(client.value.connect(host.value.local_endpoint(), 100u));
    CHECK(client.value.state() == jojo::DirectSessionState::connecting);

    const auto host_control = host.value.poll(100u);
    CHECK(host_control);
    CHECK(host.value.state() == jojo::DirectSessionState::connected);
    CHECK(host.value.remote_endpoint().has_value());
    if (host.value.remote_endpoint()) {
        CHECK(*host.value.remote_endpoint() == client.value.local_endpoint());
    }

    const auto client_control = client.value.poll(100u);
    CHECK(client_control);
    CHECK(client.value.state() == jojo::DirectSessionState::connected);
    CHECK(client.value.remote_endpoint().has_value());
    if (client.value.remote_endpoint()) {
        CHECK(*client.value.remote_endpoint() == host.value.local_endpoint());
    }

    // Process the client's immediate acknowledgement of the reliable accept.
    CHECK(host.value.poll(100u));

    auto gameplay = make_packet(jojo::NetworkPacketKind::input, 90u);
    gameplay.ack = 0u;
    CHECK(client.value.send(gameplay, 110u));
    const auto delivered = host.value.poll(110u);
    CHECK(delivered);
    if (delivered) {
        CHECK(delivered.value.size() == 1u);
        if (delivered.value.size() == 1u) CHECK(delivered.value[0] == gameplay);
    }

    CHECK(host.value.telemetry().packets_received >= 2u);
    CHECK(client.value.telemetry().packets_sent >= 2u);
}

static void test_session_ignores_spoofed_peer_and_disconnects_cleanly() {
    auto host = jojo::DirectUdpSession::bind(
        jojo::DirectSessionRole::host, jojo::NetworkEndpoint::loopback(0u));
    auto client = jojo::DirectUdpSession::bind(
        jojo::DirectSessionRole::client, jojo::NetworkEndpoint::loopback(0u));
    auto stranger = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0u));
    CHECK(host && client && stranger);
    if (!host || !client || !stranger) return;

    CHECK(client.value.connect(host.value.local_endpoint(), 200u));
    CHECK(host.value.poll(200u));
    CHECK(client.value.poll(200u));
    CHECK(host.value.poll(200u));
    CHECK(host.value.state() == jojo::DirectSessionState::connected);

    auto fake_disconnect = make_packet(jojo::NetworkPacketKind::disconnect, 999u);
    CHECK(stranger.value.send_packet(host.value.local_endpoint(), fake_disconnect));
    CHECK(host.value.poll(201u));
    CHECK(host.value.state() == jojo::DirectSessionState::connected);

    CHECK(client.value.disconnect(210u));
    CHECK(client.value.state() == jojo::DirectSessionState::disconnected);
    CHECK(host.value.poll(210u));
    CHECK(host.value.state() == jojo::DirectSessionState::disconnected);
    CHECK(host.value.telemetry().state == jojo::NetworkConnectionState::disconnected);
}

#endif

int main() {
#if JOJO_HAS_NETWORK_TRANSPORT
    test_loopback_udp_is_nonblocking_and_preserves_datagrams();
    test_transport_bounds_datagrams_and_parses_network_packets();
    test_direct_session_handshake_and_gameplay_packet_round_trip();
    test_session_ignores_spoofed_peer_and_disconnects_cleanly();
#else
    std::cerr << "R2.5 RED: core/network_transport.h is not implemented\n";
    return 1;
#endif
    if (failures) {
        std::cerr << failures << " network transport assertion(s) failed\n";
        return 1;
    }
    std::cout << "R2.5 direct UDP transport assertions passed\n";
    return 0;
}
