#pragma once
#include <string>
#include <vector>

namespace jojo::win32 {
enum class DeviceKind { keyboard, xinput, hid };
struct InputDevice {
    std::string id;
    std::wstring name;
    DeviceKind kind{DeviceKind::keyboard};
};
[[nodiscard]] std::vector<InputDevice> enumerate_input_devices();
}
