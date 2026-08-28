#include "core/input.h"
#include <array>

namespace jojo {

const std::vector<GameAction>& all_game_actions() {
    static const std::vector<GameAction> actions{
        GameAction::up, GameAction::down, GameAction::left, GameAction::right,
        GameAction::attack_light, GameAction::attack_medium, GameAction::attack_heavy,
        GameAction::stand, GameAction::start, GameAction::coin, GameAction::pause
    };
    return actions;
}

std::string to_string(GameAction action) {
    switch (action) {
        case GameAction::up: return "up";
        case GameAction::down: return "down";
        case GameAction::left: return "left";
        case GameAction::right: return "right";
        case GameAction::attack_light: return "attack_light";
        case GameAction::attack_medium: return "attack_medium";
        case GameAction::attack_heavy: return "attack_heavy";
        case GameAction::stand: return "stand";
        case GameAction::start: return "start";
        case GameAction::coin: return "coin";
        case GameAction::pause: return "pause";
    }
    return {};
}

Result<GameAction> game_action_from_string(std::string_view value) {
    for (const auto action : all_game_actions()) {
        if (to_string(action) == value) return Result<GameAction>::success(action);
    }
    return Result<GameAction>::failure(ErrorCode::invalid_settings, "unknown game action: " + std::string(value));
}

std::string to_string(BindingKind kind) {
    switch (kind) {
        case BindingKind::keyboard_key: return "key";
        case BindingKind::gamepad_button: return "button";
        case BindingKind::gamepad_axis: return "axis";
    }
    return {};
}

Result<BindingKind> binding_kind_from_string(std::string_view value) {
    if (value == "key") return Result<BindingKind>::success(BindingKind::keyboard_key);
    if (value == "button") return Result<BindingKind>::success(BindingKind::gamepad_button);
    if (value == "axis") return Result<BindingKind>::success(BindingKind::gamepad_axis);
    return Result<BindingKind>::failure(ErrorCode::invalid_settings, "unknown binding kind: " + std::string(value));
}

std::string serialize_binding(const InputBinding& binding) {
    return binding.device_id + "|" + to_string(binding.kind) + "|" + binding.code;
}

Result<InputBinding> parse_binding(std::string_view value) {
    const auto first = value.find('|');
    if (first == std::string_view::npos) {
        return Result<InputBinding>::failure(ErrorCode::invalid_settings, "binding is missing kind separator");
    }
    const auto second = value.find('|', first + 1);
    if (second == std::string_view::npos) {
        return Result<InputBinding>::failure(ErrorCode::invalid_settings, "binding is missing code separator");
    }
    const auto device = value.substr(0, first);
    const auto kind_text = value.substr(first + 1, second - first - 1);
    const auto code = value.substr(second + 1);
    if (device.empty() || code.empty()) {
        return Result<InputBinding>::failure(ErrorCode::invalid_settings, "binding device/code cannot be empty");
    }
    auto kind = binding_kind_from_string(kind_text);
    if (!kind) return Result<InputBinding>::failure(kind.error, kind.detail);
    return Result<InputBinding>::success(InputBinding{std::string(device), kind.value, std::string(code)});
}

std::map<GameAction, InputBinding> default_bindings() {
    const std::string keyboard = "keyboard:default";
    return {
        {GameAction::up, {keyboard, BindingKind::keyboard_key, "ArrowUp"}},
        {GameAction::down, {keyboard, BindingKind::keyboard_key, "ArrowDown"}},
        {GameAction::left, {keyboard, BindingKind::keyboard_key, "ArrowLeft"}},
        {GameAction::right, {keyboard, BindingKind::keyboard_key, "ArrowRight"}},
        {GameAction::attack_light, {keyboard, BindingKind::keyboard_key, "Z"}},
        {GameAction::attack_medium, {keyboard, BindingKind::keyboard_key, "X"}},
        {GameAction::attack_heavy, {keyboard, BindingKind::keyboard_key, "C"}},
        {GameAction::stand, {keyboard, BindingKind::keyboard_key, "A"}},
        {GameAction::start, {keyboard, BindingKind::keyboard_key, "Enter"}},
        {GameAction::coin, {keyboard, BindingKind::keyboard_key, "RightShift"}},
        {GameAction::pause, {keyboard, BindingKind::keyboard_key, "Escape"}},
    };
}

InputSettings::InputSettings() : bindings(default_bindings()) {}

}
