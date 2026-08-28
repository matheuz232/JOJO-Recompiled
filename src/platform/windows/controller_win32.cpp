#ifdef _WIN32
#define NOMINMAX
#include "platform/windows/controller_win32.h"
#include "core/device_id.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <xinput.h>
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <utility>
#include <memory>
#include <set>
#include <vector>

namespace jojo::win32 {
namespace {
using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

std::wstring lower(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}

std::string narrow_utf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                        out.data(), count, nullptr, nullptr);
    return out;
}

void add_xinput(std::vector<InputDevice>& out) {
    constexpr const wchar_t* dlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    HMODULE module = nullptr;
    for (const auto* dll : dlls) {
        module = LoadLibraryW(dll);
        if (module) break;
    }
    if (!module) return;
    const auto get_state = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
    if (get_state) {
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE state{};
            if (get_state(i, &state) == ERROR_SUCCESS) {
                out.push_back({make_xinput_device_id(i), L"Controle XInput " + std::to_wstring(i + 1), DeviceKind::xinput});
            }
        }
    }
    FreeLibrary(module);
}

void add_hid(std::vector<InputDevice>& out) {
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);
    HDEVINFO info = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return;

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &hid_guid, index, &iface)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        DWORD required = 0;
        SP_DEVINFO_DATA devinfo{};
        devinfo.cbSize = sizeof(devinfo);
        SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &required, &devinfo);
        if (!required) continue;
        std::vector<std::byte> buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, nullptr, &devinfo)) continue;

        std::wstring path = detail->DevicePath;
        if (lower(path).find(L"ig_") != std::wstring::npos) continue; // avoid XInput duplicate HID endpoints

        wchar_t name[512]{};
        DWORD type = 0;
        DWORD bytes = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(info, &devinfo, SPDRP_FRIENDLYNAME,
                                               &type, reinterpret_cast<PBYTE>(name), sizeof(name), &bytes)) {
            SetupDiGetDeviceRegistryPropertyW(info, &devinfo, SPDRP_DEVICEDESC,
                                              &type, reinterpret_cast<PBYTE>(name), sizeof(name), &bytes);
        }
        const std::wstring display = name[0] ? name : L"Controle HID";
        out.push_back({make_hid_device_id(narrow_utf8(path)), display, DeviceKind::hid});
    }
    SetupDiDestroyDeviceInfoList(info);
}
}

std::vector<InputDevice> enumerate_input_devices() {
    std::vector<InputDevice> devices;
    devices.push_back({"keyboard:default", L"Teclado", DeviceKind::keyboard});
    add_xinput(devices);
    add_hid(devices);

    std::set<std::string> seen;
    std::vector<InputDevice> unique;
    for (auto& device : devices) {
        if (seen.insert(device.id).second) unique.push_back(std::move(device));
    }
    return unique;
}
#endif
