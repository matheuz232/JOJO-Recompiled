#include "core/conversion.h"
#include "core/device_id.h"
#include "core/disc_image.h"
#include "core/input.h"
#include "core/network_protocol.h"
#include "core/online_session.h"
#include "core/presentation.h"
#include "core/revision.h"
#include "core/runtime.h"
#include "core/settings.h"
#include "core/training.h"
#include "core/version.h"
#include "iso_fixture.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_file(std::string_view name) {
    auto p = fs::temp_directory_path() / (std::string("jojo_recompiled_") + std::string(name));
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}

static void test_version_shape() {
    const auto version = jojo::core_version();
    CHECK(version.find('.') != std::string::npos);
}

static void test_settings_round_trip() {
    const auto path = temp_file("settings.ini");
    jojo::Settings expected{};
    expected.display_mode = jojo::DisplayMode::borderless;
    expected.vsync = false;
    expected.frame_cap = 120;
    expected.internal_scale = 3;
    expected.master_volume = 0.75f;
    expected.music_volume = 0.35f;
    expected.sfx_volume = 0.90f;
    expected.input_delay_frames = 2;
    expected.rollback_frames = 8;
    CHECK(jojo::save_settings(path, expected));
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.display_mode == expected.display_mode);
        CHECK(loaded.value.vsync == expected.vsync);
        CHECK(loaded.value.frame_cap == expected.frame_cap);
        CHECK(loaded.value.internal_scale == expected.internal_scale);
        CHECK(std::fabs(loaded.value.master_volume - expected.master_volume) < 0.001f);
        CHECK(std::fabs(loaded.value.music_volume - expected.music_volume) < 0.001f);
        CHECK(std::fabs(loaded.value.sfx_volume - expected.sfx_volume) < 0.001f);
        CHECK(loaded.value.input_delay_frames == expected.input_delay_frames);
        CHECK(loaded.value.rollback_frames == expected.rollback_frames);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_settings_clamp_invalid_values() {
    const auto path = temp_file("settings_invalid.ini");
    {
        std::ofstream out(path);
        out << "display_mode=fullscreen\n";
        out << "vsync=no\n";
        out << "frame_cap=999\n";
        out << "internal_scale=0\n";
        out << "master_volume=4\n";
        out << "music_volume=-3\n";
        out << "sfx_volume=0.5\n";
        out << "input_delay_frames=99\n";
        out << "rollback_frames=99\n";
    }
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.display_mode == jojo::DisplayMode::fullscreen);
        CHECK(!loaded.value.vsync);
        CHECK(loaded.value.frame_cap == 360);
        CHECK(loaded.value.internal_scale == 1);
        CHECK(loaded.value.master_volume == 1.0f);
        CHECK(loaded.value.music_volume == 0.0f);
        CHECK(loaded.value.sfx_volume == 0.5f);
        CHECK(loaded.value.input_delay_frames == 6);
        CHECK(loaded.value.rollback_frames == 12);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_presentation_viewport() {
    jojo::PresentationConfig cfg{};
    cfg.design_width = 640;
    cfg.design_height = 480;
    cfg.window_width = 1920;
    cfg.window_height = 1080;
    const auto vp = jojo::calculate_letterbox_viewport(cfg);
    CHECK(vp.width == 1440);
    CHECK(vp.height == 1080);
    CHECK(vp.x == 240);
    CHECK(vp.y == 0);
}

static void test_input_state_and_bindings() {
    jojo::InputState state{};
    state.buttons = jojo::InputButton::left | jojo::InputButton::light_punch;
    state.left_x = 0.5f;
    CHECK(jojo::has_button(state, jojo::InputButton::left));
    CHECK(jojo::has_button(state, jojo::InputButton::light_punch));
    CHECK(!jojo::has_button(state, jojo::InputButton::right));
    CHECK(std::fabs(jojo::apply_deadzone(0.1f, 0.2f)) < 0.001f);
    CHECK(jojo::apply_deadzone(0.9f, 0.2f) > 0.8f);
}

static void test_network_packet_round_trip() {
    jojo::InputPacket input{};
    input.frame = 42;
    input.buttons = 0xA5A5;
    input.left_x = 1234;
    input.left_y = -2345;
    input.right_x = 3210;
    input.right_y = -4321;
    const auto bytes = jojo::serialize_input_packet(input);
    const auto parsed = jojo::parse_input_packet(bytes);
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.frame == input.frame);
        CHECK(parsed.value.buttons == input.buttons);
        CHECK(parsed.value.left_x == input.left_x);
        CHECK(parsed.value.left_y == input.left_y);
        CHECK(parsed.value.right_x == input.right_x);
        CHECK(parsed.value.right_y == input.right_y);
    }
}

static void test_online_session_frame_flow() {
    jojo::OnlineSession session{};
    session.set_local_input_delay(2);
    jojo::InputState local{};
    local.buttons = jojo::InputButton::heavy_punch;
    session.submit_local_input(10, local);
    CHECK(session.local_input_for_frame(10).buttons == jojo::InputButton::none);
    CHECK(jojo::has_button(session.local_input_for_frame(12), jojo::InputButton::heavy_punch));
}

static void test_training_state_helpers() {
    jojo::TrainingState state{};
    jojo::apply_training_preset(state, jojo::TrainingPreset::full_meter);
    CHECK(state.player1_meter == state.max_meter);
    CHECK(state.player2_meter == state.max_meter);
    state.player1_health = 10;
    jojo::reset_training_positions(state);
    CHECK(state.player1_health == state.max_health);
}

static void test_disc_fingerprint_and_extensions() {
    CHECK(jojo::supported_disc_extension("game.iso"));
    CHECK(jojo::supported_disc_extension("GAME.BIN"));
    CHECK(jojo::supported_disc_extension("disc.cue"));
    CHECK(!jojo::supported_disc_extension("disc.gdi"));
    CHECK(!jojo::supported_disc_extension("game.zip"));

    const auto path = temp_file("disc.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out << "JOJO";
    }
    const auto fp = jojo::fingerprint_disc_image(path);
    CHECK(fp);
    if (fp) {
        CHECK(fp.value.format == "bin");
        CHECK(fp.value.size_bytes == 4);
        CHECK(fp.value.hash_hex.size() == 16);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_conversion_requires_known_revision_by_default() {
    const auto source = temp_file("conversion_unknown.iso");
    test_iso::write_image(source);
    const auto install = fs::temp_directory_path() / "jojo_recompiled_conversion_unknown";
    std::error_code ec;
    fs::remove_all(install, ec);
    const auto result = jojo::convert_image(source, install, jojo::ConversionOptions{});
    CHECK(!result);
    if (!result) CHECK(result.error == jojo::ErrorCode::unknown_revision);
    fs::remove(source, ec);
    fs::remove_all(install, ec);
}

static void test_conversion_unverified_base_path() {
    const auto source = temp_file("conversion_unverified.iso");
    test_iso::write_image(source);
    const auto install = fs::temp_directory_path() / "jojo_recompiled_conversion_unverified";
    std::error_code ec;
    fs::remove_all(install, ec);
    jojo::ConversionOptions options{};
    options.allow_unverified_base_conversion = true;
    const auto result = jojo::convert_image(source, install, options);
    CHECK(result);
    if (result) {
        CHECK(result.value.manifest_version == "1");
        CHECK(result.value.backend == "pending-game-specific-recompiler");
        CHECK(result.value.revision_id.rfind("unverified-fnv1a64-", 0) == 0);
    }
    fs::remove(source, ec);
    fs::remove_all(install, ec);
}

static void test_conversion_progress_is_monotonic() {
    const auto source = temp_file("conversion_progress.iso");
    test_iso::write_image(source);
    const auto install = fs::temp_directory_path() / "jojo_recompiled_conversion_progress";
    std::error_code ec;
    fs::remove_all(install, ec);
    jojo::ConversionOptions options{};
    options.allow_unverified_base_conversion = true;
    std::vector<int> percents;
    const auto result = jojo::convert_image(source, install, options,
        [&](const jojo::ConversionProgress& progress) { percents.push_back(progress.percent); });
    CHECK(result);
    CHECK(!percents.empty());
    CHECK(std::is_sorted(percents.begin(), percents.end()));
    CHECK(percents.back() == 100);
    fs::remove(source, ec);
    fs::remove_all(install, ec);
}

static void test_conversion_rejects_gdi() {
    const auto gdi = temp_file("conversion_reject.gdi");
    {
        std::ofstream out(gdi);
        out << "1\n1 0 4 2352 track.bin 0\n";
    }
    const auto install = fs::temp_directory_path() / "jojo_recompiled_conversion_gdi";
    std::error_code ec;
    fs::remove_all(install, ec);
    const auto result = jojo::convert_image(gdi, install);
    CHECK(!result);
    if (!result) CHECK(result.error == jojo::ErrorCode::unsupported_format);
    fs::remove(gdi, ec);
    fs::remove_all(install, ec);
}

static void test_runtime_installation_validation() {
    const auto install = fs::temp_directory_path() / "jojo_recompiled_runtime_test";
    std::error_code ec;
    fs::remove_all(install, ec);
    const auto missing = jojo::validate_installation(install);
    CHECK(!missing);

    fs::create_directories(install / "data");
    fs::create_directories(install / "cache");
    jojo::ConversionManifest m{};
    m.manifest_version = "1";
    m.converter_version = jojo::core_version();
    m.source_name = "owned.iso";
    m.source_format = "iso";
    m.source_size = 1234;
    m.hash_hex = "0123456789abcdef";
    CHECK(jojo::save_conversion_manifest_atomic(install / "game_manifest.ini", m));

    const auto valid = jojo::validate_installation(install);
    CHECK(valid);
    const auto boot = jojo::bootstrap_runtime(install);
    CHECK(!boot);
    CHECK(boot.error == jojo::ErrorCode::backend_unavailable);
    fs::remove_all(install, ec);
}

static void test_device_id_helpers_are_stable() {
    CHECK(jojo::make_xinput_device_id(0) == "xinput:0");
    CHECK(jojo::make_xinput_device_id(3) == "xinput:3");
    const auto a = jojo::make_hid_device_id(R"(\\?\hid#vid_1234&pid_abcd#one)");
    const auto b = jojo::make_hid_device_id(R"(\\?\HID#VID_1234&PID_ABCD#ONE)");
    CHECK(a == b);
    CHECK(a.rfind("hid:", 0) == 0);
}

int main() {
    test_version_shape();
    test_settings_round_trip();
    test_settings_clamp_invalid_values();
    test_presentation_viewport();
    test_input_state_and_bindings();
    test_network_packet_round_trip();
    test_online_session_frame_flow();
    test_training_state_helpers();
    test_disc_fingerprint_and_extensions();
    test_conversion_requires_known_revision_by_default();
    test_conversion_unverified_base_path();
    test_conversion_progress_is_monotonic();
    test_conversion_rejects_gdi();
    test_runtime_installation_validation();
    test_device_id_helpers_are_stable();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "all core assertions passed\n";
    return 0;
}
