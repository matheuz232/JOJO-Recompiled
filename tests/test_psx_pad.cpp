#include "core/psx_pad.h"
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

int main() {
    jojo::ResolvedInputFrame input{};
    input[0].actions[jojo::GameAction::up] = true;
    input[0].actions[jojo::GameAction::left] = true;
    input[0].actions[jojo::GameAction::attack_light] = true;
    input[0].actions[jojo::GameAction::stand] = true;
    input[0].actions[jojo::GameAction::pause] = true;

    input[1].actions[jojo::GameAction::right] = true;
    input[1].actions[jojo::GameAction::down] = true;
    input[1].actions[jojo::GameAction::attack_medium] = true;
    input[1].actions[jojo::GameAction::attack_heavy] = true;
    input[1].actions[jojo::GameAction::start] = true;

    const auto pads = jojo::make_psx_digital_pad_frame(input);

    // Digital pad bits are active-low:
    // bit3 Start, bits4-7 directions, bits12-15 Triangle/Circle/Cross/Square.
    CHECK((pads[0].buttons & (1u << 3u)) == 0u);
    CHECK((pads[0].buttons & (1u << 4u)) == 0u);
    CHECK((pads[0].buttons & (1u << 7u)) == 0u);
    CHECK((pads[0].buttons & (1u << 14u)) == 0u); // Cross = Stand
    CHECK((pads[0].buttons & (1u << 15u)) == 0u); // Square = Light
    CHECK((pads[0].buttons & (1u << 5u)) != 0u);
    CHECK((pads[0].buttons & (1u << 12u)) != 0u);

    CHECK((pads[1].buttons & (1u << 3u)) == 0u);
    CHECK((pads[1].buttons & (1u << 5u)) == 0u);
    CHECK((pads[1].buttons & (1u << 6u)) == 0u);
    CHECK((pads[1].buttons & (1u << 12u)) == 0u); // Triangle = Medium
    CHECK((pads[1].buttons & (1u << 13u)) == 0u); // Circle = Heavy
    CHECK((pads[1].buttons & (1u << 15u)) != 0u);

    const auto response = jojo::psx_digital_pad_poll_response(pads[0]);
    CHECK(response[0] == 0xffu);
    CHECK(response[1] == 0x41u);
    CHECK(response[2] == 0x5au);
    CHECK(response[3] == static_cast<std::uint8_t>(pads[0].buttons));
    CHECK(response[4] == static_cast<std::uint8_t>(pads[0].buttons >> 8u));

    jojo::ResolvedPlayerInput idle{};
    const auto idle_pad = jojo::make_psx_digital_pad_state(idle);
    CHECK(idle_pad.buttons == 0xffffu);
    const auto idle_response = jojo::psx_digital_pad_poll_response(idle_pad);
    CHECK(idle_response[3] == 0xffu);
    CHECK(idle_response[4] == 0xffu);

    if (failures) return 1;
    std::cout << "PS1 two-player digital pad mapping assertions passed\n";
    return 0;
}
