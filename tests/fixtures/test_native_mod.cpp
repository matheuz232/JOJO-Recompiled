#include "mod_api/jojo_mod_api.h"

#include <cstdlib>
#include <fstream>
#include <string>

#ifndef JOJO_TEST_NATIVE_MOD_ID
#define JOJO_TEST_NATIVE_MOD_ID "native.fixture"
#endif

#ifndef JOJO_TEST_NATIVE_MOD_ABI
#define JOJO_TEST_NATIVE_MOD_ABI JOJO_NATIVE_MOD_ABI_V1
#endif

#ifndef JOJO_TEST_NATIVE_MOD_LOAD_RESULT
#define JOJO_TEST_NATIVE_MOD_LOAD_RESULT 0
#endif

namespace {

#if !defined(JOJO_TEST_NATIVE_MOD_NO_EXPORT)
std::string native_mod_test_log_path() {
#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&raw, &length, "JOJO_NATIVE_MOD_TEST_LOG") != 0 || raw == nullptr) {
        return {};
    }
    std::string path(raw);
    std::free(raw);
    return path;
#else
    const char* raw = std::getenv("JOJO_NATIVE_MOD_TEST_LOG");
    return raw == nullptr ? std::string{} : std::string(raw);
#endif
}

void append_event(const char* action) {
    const auto log_path = native_mod_test_log_path();
    if (log_path.empty()) return;

    std::ofstream out(log_path, std::ios::app);
    out << action << ':' << JOJO_TEST_NATIVE_MOD_ID << '\n';
}

int on_load() {
    append_event("load");
    return JOJO_TEST_NATIVE_MOD_LOAD_RESULT;
}

void on_unload() {
    append_event("unload");
}
#endif

}

#if !defined(JOJO_TEST_NATIVE_MOD_NO_EXPORT)
#if defined(_WIN32)
#define JOJO_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define JOJO_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

JOJO_TEST_EXPORT const JojoNativeModV1* jojo_get_native_mod_v1(void) {
    static const JojoNativeModV1 descriptor{
        static_cast<uint32_t>(sizeof(JojoNativeModV1)),
        JOJO_TEST_NATIVE_MOD_ABI,
        JOJO_TEST_NATIVE_MOD_ID,
        &on_load,
        &on_unload,
    };
    return &descriptor;
}
#else
extern "C" int jojo_native_mod_fixture_without_entry_point(void) {
    return 0;
}
#endif
