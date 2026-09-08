#include "core/conversion.h"
#include "iso_fixture.h"
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    const auto source = fs::temp_directory_path() / "jojo_unverified_conversion.iso";
    const auto install = fs::temp_directory_path() / "jojo_unverified_conversion_install";
    std::error_code ec;
    fs::remove(source, ec);
    fs::remove_all(install, ec);
    test_iso::write_image(source);

    const auto converted = jojo::convert_image(source, install);
    if (!converted) {
        std::cerr << "default conversion rejected a valid image without registered profiles: "
                  << converted.detail << '\n';
        fs::remove(source, ec);
        fs::remove_all(install, ec);
        return 1;
    }

    if (converted.value.revision_id.rfind("unverified-fnv1a64-", 0) != 0) {
        std::cerr << "unexpected unverified revision id: " << converted.value.revision_id << '\n';
        return 1;
    }
    if (converted.value.backend != "pending-game-specific-recompiler") {
        std::cerr << "unverified conversion must not become native-ready\n";
        return 1;
    }

    const auto manifest = jojo::load_conversion_manifest(install / "game_manifest.ini");
    if (!manifest || manifest.value.revision_id != converted.value.revision_id ||
        manifest.value.backend != "pending-game-specific-recompiler") {
        std::cerr << "unverified conversion manifest is missing or inconsistent\n";
        return 1;
    }

    jojo::ConversionOptions strict{};
    const auto strict_result = jojo::convert_image(source, install, strict);
    if (strict_result || strict_result.error != jojo::ErrorCode::unknown_revision) {
        std::cerr << "explicit strict conversion must still reject unknown revisions\n";
        return 1;
    }

    fs::remove(source, ec);
    fs::remove_all(install, ec);
    std::cout << "unverified base conversion contract passed\n";
    return 0;
}
