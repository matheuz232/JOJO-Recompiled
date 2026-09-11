#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cwchar>
#include <iostream>
#include <string>
#include <thread>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

namespace {
using namespace std::chrono_literals;
int failures = 0;
std::atomic_bool application_exited{false};
constexpr int ID_RUN_OMEGA_INFINITY = 1007;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
    return condition;
}

template<class Predicate>
bool wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    do {
        if (predicate()) return true;
        if (application_exited) return false;
        std::this_thread::sleep_for(20ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

struct WindowSearch {
    const wchar_t* class_name;
    HWND result{};
};

BOOL CALLBACK find_window(HWND window, LPARAM param) {
    auto& search = *reinterpret_cast<WindowSearch*>(param);
    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, 128);
    if (IsWindowVisible(window) && std::wcscmp(class_name, search.class_name) == 0) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}

HWND thread_window(DWORD thread_id, const wchar_t* class_name) {
    WindowSearch search{class_name};
    EnumThreadWindows(thread_id, find_window, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

std::wstring window_text(HWND window) {
    const auto length = static_cast<size_t>(GetWindowTextLengthW(window));
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    text.resize(length);
    return text;
}

void inspect_application(DWORD ui_thread) {
    HWND window = nullptr;
    if (!check(wait_until([&] {
            window = thread_window(ui_thread, L"JOJORecompiledWindow");
            return window != nullptr;
        }), "application window exists")) {
        PostThreadMessageW(ui_thread, WM_QUIT, 1, 0);
        return;
    }

    const auto omega = GetDlgItem(window, ID_RUN_OMEGA_INFINITY);
    if (check(omega != nullptr, "OMEGA Infinity button control 1007 exists")) {
        check(GetParent(omega) == window && IsWindowVisible(omega),
              "OMEGA Infinity button is visible child of application window");
        RECT bounds{}, client{};
        GetWindowRect(omega, &bounds);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
        GetClientRect(window, &client);
        check(bounds.right > bounds.left && bounds.bottom > bounds.top &&
                  bounds.left >= 0 && bounds.top >= 0 &&
                  bounds.right <= client.right && bounds.bottom <= client.bottom,
              "OMEGA Infinity button lies inside application client area");
        check(window_text(omega) == L"EXECUTAR OMEGA INFINITY",
              "OMEGA Infinity button has exact initial label");
        check(!IsWindowEnabled(omega),
              "OMEGA Infinity button is disabled without valid PS1 installation");
    }

    PostMessageW(window, WM_CLOSE, 0, 0);
}
} // namespace

int main() {
    const DWORD ui_thread = GetCurrentThreadId();
    std::thread observer([&] { inspect_application(ui_thread); });
    wchar_t arguments[] = L"";
    const int result = wWinMain(GetModuleHandleW(nullptr), nullptr, arguments, SW_SHOWNORMAL);
    application_exited = true;
    observer.join();
    check(result == 0, "application shuts down normally");
    if (failures) {
        std::cerr << failures << " Win32 OMEGA-control assertion(s) failed\n";
        return 1;
    }
    return 0;
}
#endif
