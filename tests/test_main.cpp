#include "core/version.h"
#include "core/settings.h"
#include "core/input.h"
#include "core/disc_image.h"
#include "core/conversion.h"
#include "core/iso9660.h"
#include "core/runtime.h"
#include "core/device_id.h"
#include "iso_fixture.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_file(std::string_view name) {
    auto p = fs::temp_directory_path() / (std::string("jojo_recompiled_") + std::string(name));
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(p.string() + ".tmp", ec);
    return p;
}

static jojo::GameRevisionProfile synthetic_revision_profile() {
    return {
        "synthetic-test-revision",
        {
            {"/1ST_READ.BIN", 12, 0x87ee7cce3a6a6a10ull},
            {"/DATA/ASSET.DAT", 5, 0x65f9a54a4f1d65c8ull},
        }
    };
}

static jojo::ConversionOptions synthetic_conversion_options() {
    jojo::ConversionOptions options{};
    options.revision_profiles.push_back(synthetic_revision_profile());
    return options;
}

static std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

static void test_version() {
    CHECK(!std::string_view(jojo::core_version()).empty());
}

static void test_graphics_defaults_are_valid() {
    const jojo::GraphicsSettings g{};
    CHECK(jojo::validate_graphics(g));
    CHECK(g.width >= 640);
    CHECK(g.height >= 480);
}

static void test_graphics_rejects_invalid_options() {
    jojo::GraphicsSettings g{};
    g.width = 9000;
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.msaa = static_cast<jojo::Msaa>(16);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.texture_filter = static_cast<jojo::TextureFilter>(32);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.aspect_ratio = static_cast<jojo::AspectRatio>(99);
    CHECK(!jojo::validate_graphics(g));
}

static void test_settings_round_trip() {
    const auto path = temp_file("settings.ini");
    jojo::AppSettings in{};
    in.install_dir = "C:/Games/JOJO-Recompiled";
    in.graphics.width = 7680;
    in.graphics.height = 4320;
    in.graphics.aspect_ratio = jojo::AspectRatio::ratio_32_9;
    in.graphics.texture_filter = jojo::TextureFilter::x16;
    in.graphics.msaa = jojo::Msaa::x4;
    in.graphics.display_mode = jojo::DisplayMode::fullscreen;
    in.graphics.vsync = false;

    const auto saved = jojo::save_settings_atomic(path, in);
    CHECK(saved);
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.install_dir == in.install_dir);
        CHECK(loaded.value.graphics == in.graphics);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_graphics_extended_options_round_trip() {
    const auto path = temp_file("graphics_extended.ini");
    jojo::AppSettings in{};
    in.graphics.width = 3840;
    in.graphics.height = 2160;
    in.graphics.msaa = jojo::Msaa::x8;
    in.graphics.display_mode = jojo::DisplayMode::borderless;
    in.graphics.ui_scale = jojo::UiScale::percent_80;
    in.graphics.hud_safe_area = jojo::HudSafeArea::safe_16_9;
    CHECK(jojo::validate_graphics(in.graphics));
    CHECK(jojo::save_settings_atomic(path, in));
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.graphics.msaa == jojo::Msaa::x8);
        CHECK(loaded.value.graphics.display_mode == jojo::DisplayMode::borderless);
        CHECK(loaded.value.graphics.ui_scale == jojo::UiScale::percent_80);
        CHECK(loaded.value.graphics.hud_safe_area == jojo::HudSafeArea::safe_16_9);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_graphics_rejects_unknown_extended_options() {
    jojo::GraphicsSettings g{};
    g.display_mode = static_cast<jojo::DisplayMode>(99);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.ui_scale = static_cast<jojo::UiScale>(999);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.hud_safe_area = static_cast<jojo::HudSafeArea>(99);
    CHECK(!jojo::validate_graphics(g));
}

static void test_input_bindings_round_trip() {
    const auto path = temp_file("input_settings.ini");
    jojo::AppSettings in{};
    in.input.selected_device = "xinput:0";
    in.input.bindings[jojo::GameAction::up] = {"xinput:0", jojo::BindingKind::gamepad_button, "DPAD_UP"};
    in.input.bindings[jojo::GameAction::attack_light] = {"xinput:0", jojo::BindingKind::gamepad_button, "A"};
    in.input.bindings[jojo::GameAction::pause] = {"keyboard:default", jojo::BindingKind::keyboard_key, "Escape"};
    CHECK(jojo::save_settings_atomic(path, in));
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.input.selected_device == "xinput:0");
        CHECK(loaded.value.input.bindings.at(jojo::GameAction::attack_light).code == "A");
        CHECK(loaded.value.input.bindings.at(jojo::GameAction::pause).kind == jojo::BindingKind::keyboard_key);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_disc_extension_detection() {
    CHECK(jojo::supported_disc_extension("game.ISO"));
    CHECK(jojo::supported_disc_extension("game.bin"));
    CHECK(jojo::supported_disc_extension("game.cue"));
    CHECK(jojo::supported_disc_extension("game.gdi"));
    CHECK(!jojo::supported_disc_extension("game.zip"));
}

static void test_disc_fingerprint_is_deterministic() {
    const auto path = temp_file("disc.iso");
    {
        std::ofstream out(path, std::ios::binary);
        out << "JOJO-RECOMPILED-SYNTHETIC-DISC";
    }
    const auto a = jojo::fingerprint_disc_image(path);
    const auto b = jojo::fingerprint_disc_image(path);
    CHECK(a);
    CHECK(b);
    if (a && b) {
        CHECK(a.value.size_bytes == 30);
        CHECK(a.value.fnv1a64 == b.value.fnv1a64);
        CHECK(a.value.hash_hex == b.value.hash_hex);
        CHECK(a.value.format == "iso");
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_conversion_creates_source_independent_installation() {
    const auto source = temp_file("convert.iso");
    const auto install = fs::temp_directory_path() / "jojo_recompiled_install_test";
    std::error_code ec;
    fs::remove_all(install, ec);
    test_iso::write_image(source);

    const auto converted = jojo::convert_image(source, install, synthetic_conversion_options());
    CHECK(converted);
    CHECK(fs::exists(install / "game_manifest.ini"));
    CHECK(fs::is_directory(install / "data"));
    CHECK(fs::is_directory(install / "cache"));
    fs::remove(source, ec);
    CHECK(!fs::exists(source));
    const auto manifest = jojo::load_conversion_manifest(install / "game_manifest.ini");
    CHECK(manifest);
    if (manifest) {
        CHECK(manifest.value.source_name == source.filename().string());
        CHECK(!manifest.value.hash_hex.empty());
        CHECK(manifest.value.revision_id == "synthetic-test-revision");
        CHECK(manifest.value.backend == "pending-game-specific-recompiler");
    }
    fs::remove_all(install, ec);
}

static void test_psx_conversion_materializes_prepared_runtime() {
    const auto source = temp_file("psx_prepared.iso");
    const auto install = fs::temp_directory_path() / "jojo_recompiled_psx_prepared_test";
    std::error_code ec;
    fs::remove_all(install, ec);
    test_iso::write_psx_image(source);

    const auto image = jojo::open_iso9660(source);
    CHECK(image);
    if (!image) {
        fs::remove(source, ec);
        return;
    }
    const auto system = jojo::read_iso9660_file(image.value, "/SYSTEM.CNF");
    const auto executable = jojo::read_iso9660_file(image.value, "/SLUS_010.60");
    CHECK(system);
    CHECK(executable);
    if (!system || !executable) {
        fs::remove(source, ec);
        return;
    }

    jojo::ConversionOptions options{};
    options.validate_psx_boot = true;
    options.revision_profiles.push_back({
        "synthetic-psx-runtime",
        {
            {"/SYSTEM.CNF", system.value.size(), fnv1a64(system.value)},
            {"/SLUS_010.60", executable.value.size(), fnv1a64(executable.value)},
        }});

    const auto converted = jojo::convert_image(source, install, options);
    CHECK(converted);
    if (converted) {
        CHECK(converted.value.backend == "psx-runtime-prepared");
    }
    CHECK(fs::exists(install / "data" / "SYSTEM.CNF"));
    CHECK(fs::exists(install / "data" / "PSX.EXE"));
    const auto prepared_disc = install / "data" / "DISC.ISO";
    CHECK(fs::exists(prepared_disc));

    fs::remove(source, ec);
    CHECK(!fs::exists(source));
    const auto boot = jojo::bootstrap_runtime(install);
    CHECK(boot);

    const auto prepared_image = jojo::open_iso9660(prepared_disc);
    CHECK(prepared_image);
    if (prepared_image) {
        const auto prepared_system = jojo::read_iso9660_file(prepared_image.value, "/SYSTEM.CNF");
        const auto prepared_executable = jojo::read_iso9660_file(prepared_image.value, "/SLUS_010.60");
        CHECK(prepared_system);
        CHECK(prepared_executable);
        if (prepared_system) CHECK(prepared_system.value == system.value);
        if (prepared_executable) CHECK(prepared_executable.value == executable.value);
    }

    fs::remove_all(install, ec);
}

static void test_conversion_rejects_unknown_revision_before_installation() {
    const auto source = temp_file("unknown_revision.iso");
    const auto install = fs::temp_directory_path() / "jojo_recompiled_unknown_revision_test";
    std::error_code ec;
    fs::remove_all(install, ec);
    test_iso::write_image(source);

    jojo::ConversionOptions options{};
    const auto converted = jojo::convert_image(source, install, options);
    CHECK(!converted);
    CHECK(converted.error == jojo::ErrorCode::unknown_revision);
    CHECK(converted.detail.find("verified revision profiles") != std::string::npos);
    CHECK(!fs::exists(install / "game_manifest.ini"));

    fs::remove(source, ec);
    fs::remove_all(install, ec);
}

static void test_conversion_reports_real_monotonic_progress() {
    const auto source = temp_file("progress.iso");
    const auto install = fs::temp_directory_path() / "jojo_recompiled_progress_test";
    std::error_code ec;
    fs::remove_all(install, ec);
    test_iso::write_image(source);

    std::vector<jojo::ConversionProgress> events;
    const auto converted = jojo::convert_image(
        source, install, synthetic_conversion_options(),
        [&](const jojo::ConversionProgress& event) { events.push_back(event); });
    CHECK(converted);
    CHECK(events.size() >= 7);
    if (!events.empty()) {
        CHECK(events.front().percent == 0);
        CHECK(events.back().percent == 100);
        CHECK(events.back().stage == jojo::ConversionStage::completed);
        bool saw_filesystem = false;
        bool saw_revision = false;
        int previous = -1;
        for (const auto& event : events) {
            CHECK(event.percent >= 0);
            CHECK(event.percent <= 100);
            CHECK(event.percent >= previous);
            CHECK(!event.message_key.empty());
            saw_filesystem = saw_filesystem || event.stage == jojo::ConversionStage::discovering_filesystem;
            saw_revision = saw_revision || event.stage == jojo::ConversionStage::identifying_revision;
            previous = event.percent;
        }
        CHECK(saw_filesystem);
        CHECK(saw_revision);
    }
    fs::remove(source, ec);
    fs::remove_all(install, ec);
}

static void test_conversion_accepts_bin_cue_and_gdi_media() {
    const auto iso = temp_file("multi_media_source.iso");
    const auto raw = temp_file("multi_media_track.bin");
    const auto cue = temp_file("multi_media.cue");
    const auto gdi = temp_file("multi_media.gdi");
    test_iso::write_image(iso);
    test_iso::write_raw2352_from_iso(iso, raw, 1);

    {
        std::ofstream out(cue);
        out << "FILE \"" << raw.filename().string() << "\" BINARY\n";
        out << "  TRACK 01 MODE1/2352\n";
        out << "    INDEX 01 00:00:00\n";
    }
    {
        std::ofstream out(gdi);
        out << "1\n";
        out << "3 45000 4 2352 " << raw.filename().string() << " 0\n";
    }

    const std::vector<std::pair<fs::path, std::string>> media = {
        {raw, "bin"}, {cue, "cue"}, {gdi, "gdi"}
    };
    std::error_code ec;
    for (const auto& [source, expected_format] : media) {
        const auto install = fs::temp_directory_path() /
            ("jojo_recompiled_track_conversion_" + expected_format);
        fs::remove_all(install, ec);
        const auto converted = jojo::convert_image(source, install, synthetic_conversion_options());
        CHECK(converted);
        if (converted) {
            CHECK(converted.value.source_format == expected_format);
            CHECK(converted.value.revision_id == "synthetic-test-revision");
        }
        const auto manifest = jojo::load_conversion_manifest(install / "game_manifest.ini");
        CHECK(manifest);
        if (manifest) {
            CHECK(manifest.value.source_format == expected_format);
            CHECK(manifest.value.revision_id == "synthetic-test-revision");
        }
        fs::remove_all(install, ec);
    }

    fs::remove(iso, ec);
    fs::remove(raw, ec);
    fs::remove(cue, ec);
    fs::remove(gdi, ec);
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
    test_version();
    test_graphics_defaults_are_valid();
    test_graphics_rejects_invalid_options();
    test_settings_round_trip();
    test_graphics_extended_options_round_trip();
    test_graphics_rejects_unknown_extended_options();
    test_input_bindings_round_trip();
    test_disc_extension_detection();
    test_disc_fingerprint_is_deterministic();
    test_conversion_creates_source_independent_installation();
    test_psx_conversion_materializes_prepared_runtime();
    test_conversion_rejects_unknown_revision_before_installation();
    test_conversion_reports_real_monotonic_progress();
    test_conversion_accepts_bin_cue_and_gdi_media();
    test_runtime_installation_validation();
    test_device_id_helpers_are_stable();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "all assertions passed\n";
    return 0;
}