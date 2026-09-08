#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Link the shipping entry point rather than re-creating its initialization in a test.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
int failures = 0;
std::atomic_bool application_exited{false};

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
    HWND owner{};
    HWND result{};
};

BOOL CALLBACK find_window(HWND window, LPARAM param) {
    auto& search = *reinterpret_cast<WindowSearch*>(param);
    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, 128);
    if (IsWindowVisible(window) && std::wcscmp(class_name, search.class_name) == 0 &&
        (!search.owner || GetWindow(window, GW_OWNER) == search.owner)) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}

HWND thread_window(DWORD thread_id, const wchar_t* class_name, HWND owner = nullptr) {
    WindowSearch search{class_name, owner};
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

bool usable_control(HWND parent, HWND control) {
    if (!control || GetParent(control) != parent || !IsWindowVisible(control) ||
        !IsWindowEnabled(control)) return false;
    RECT bounds{}, client{};
    GetWindowRect(control, &bounds);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&bounds), 2);
    GetClientRect(parent, &client);
    return bounds.right > bounds.left && bounds.bottom > bounds.top &&
        bounds.left >= 0 && bounds.top >= 0 && bounds.right <= client.right &&
        bounds.bottom <= client.bottom;
}

void drop_files(HWND window, const std::vector<fs::path>& paths) {
    std::wstring names;
    for (const auto& path : paths) {
        names += path.wstring();
        names.push_back(L'\0');
    }
    names.push_back(L'\0');
    const auto bytes = names.size() * sizeof(wchar_t);
    auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + bytes);
    if (!check(memory != nullptr, "allocate synthetic file-drop payload")) return;
    auto* data = static_cast<DROPFILES*>(GlobalLock(memory));
    if (!check(data != nullptr, "lock synthetic file-drop payload")) {
        GlobalFree(memory);
        return;
    }
    data->pFiles = sizeof(DROPFILES);
    data->fWide = TRUE;
    std::memcpy(reinterpret_cast<char*>(data) + sizeof(DROPFILES), names.data(), bytes);
    GlobalUnlock(memory);
    // The real window runs in this process, so the HDROP has the same address space.
    // WM_DROPFILES transfers ownership to the receiver, which must call DragFinish.
    SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
}

void inspect_application(DWORD ui_thread) {
    HWND window = nullptr;
    if (!check(wait_until([&] {
            window = thread_window(ui_thread, L"JOJORecompiledWindow");
            return window != nullptr;
        }), "shipping entry point creates a visible application window")) {
        PostThreadMessageW(ui_thread, WM_QUIT, 1, 0);
        return;
    }

    const auto path_box = GetDlgItem(window, 1001);
    const auto select_button = GetDlgItem(window, 1002);
    const auto prepare_button = GetDlgItem(window, 1003);
    check(usable_control(window, path_box), "image path field is visible inside the application");
    const bool can_select = check(usable_control(window, select_button),
                                 "SELECT button is visible and usable inside the application");
    check(usable_control(window, prepare_button), "PREPARE button is visible and usable inside the application");
    const bool can_drop = check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0,
                               "application accepts files dropped from Explorer");

    const auto directory = fs::temp_directory_path() /
        (L"jojo-image-selection-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);
    const auto bin = directory / L"JoJo teste \u00e7.BIN";
    { std::ofstream file(bin, std::ios::binary); file.put('\0'); }

    if (can_drop && path_box) {
        for (const auto* extension : {L".iso", L".cue", L".gdi", L".BIN"}) {
            const auto image = directory / (std::wstring(L"JoJo teste \u00e7") + extension);
            { std::ofstream file(image, std::ios::binary); file.put('\0'); }
            drop_files(window, {image});
            check(window_text(path_box) == image.wstring(),
                  "dropping a supported image selects its complete Unicode path");
        }
        const auto selected = window_text(path_box);
        const auto archive = directory / L"image.zip";
        { std::ofstream file(archive); file.put('\0'); }
        drop_files(window, {archive});
        check(window_text(path_box) == selected, "unsupported drops preserve the selected image");
        drop_files(window, {directory / L"missing.bin"});
        check(window_text(path_box) == selected, "missing files do not replace the selected image");
        const auto folder = directory / L"directory.bin";
        fs::create_directory(folder);
        drop_files(window, {folder});
        check(window_text(path_box) == selected, "directories do not replace the selected image");
        drop_files(window, {bin, directory / L"JoJo teste \u00e7.iso"});
        check(window_text(path_box) == selected, "multiple dropped files are not silently reduced to one");
    }

    if (can_select) {
        const auto before = window_text(path_box);
        PostMessageW(select_button, BM_CLICK, 0, 0);
        HWND picker = nullptr;
        if (check(wait_until([&] {
                picker = thread_window(ui_thread, L"#32770", window);
                return picker != nullptr;
            }), "clicking SELECT opens the native file picker")) {
            PostMessageW(picker, WM_CLOSE, 0, 0);
            check(wait_until([&] { return !IsWindow(picker) && IsWindowEnabled(window); }),
                  "cancelling the picker returns to the application");
            check(window_text(path_box) == before, "cancelling the picker preserves the selected image");
        }
    }

    std::error_code error;
    fs::remove_all(directory, error);
    PostMessageW(window, WM_CLOSE, 0, 0);
}
}

int main() {
    const DWORD ui_thread = GetCurrentThreadId();
    std::thread observer([&] {
        try {
            inspect_application(ui_thread);
        } catch (const std::exception& error) {
            check(false, error.what());
            PostThreadMessageW(ui_thread, WM_QUIT, 1, 0);
        }
    });
    wchar_t arguments[] = L"";
    const int result = wWinMain(GetModuleHandleW(nullptr), nullptr, arguments, SW_SHOWNORMAL);
    application_exited = true;
    observer.join();
    check(result == 0, "application shuts down normally");
    if (failures) {
        std::cerr << failures << " Win32 image-selection assertion(s) failed\n";
        return 1;
    }
    std::cout << "Win32 image-selection assertions passed\n";
    return 0;
}
#endif
