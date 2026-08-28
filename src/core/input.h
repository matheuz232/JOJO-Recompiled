#pragma once
#include "core/result.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

enum class GameAction {
    up,
    down,
    left,
    right,
    attack_light,
    attack_medium,
    attack_heavy,
    stand,
    start,
    coin,
    pause
};

enum class BindingKind {
    keyboard_key,
    gamepad_button,
    gamepad_axis
};

struct InputBinding {
    std::string device_id;
    BindingKind kind{BindingKind::keyboard_key};
    std::string code;
    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputSettings {
    std::string selected_device{"keyboard:default"};
    std::map<GameAction, InputBinding> bindings;
    InputSettings();
};

[[nodiscard]] const std::vector<GameAction>& all_game_actions();
[[nodiscard]] std::string to_string(GameAction action);
[[nodiscard]] Result<GameAction> game_action_from_string(std::string_view value);
[[nodiscard]] std::string to_string(BindingKind kind);
[[nodiscard]] Result<BindingKind> binding_kind_from_string(std::string_view value);
[[nodiscard]] std::string serialize_binding(const InputBinding& binding);
[[nodiscard]] Result<InputBinding> parse_binding(std::string_view value);
[[nodiscard]] std::map<GameAction, InputBinding> default_bindings();

}
