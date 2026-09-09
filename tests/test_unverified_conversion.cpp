#include "core/conversion.h"
#include "iso_fixture.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const auto source = fs::temp_directory_path() / "jojo_unknown_ps1_conversion.iso";
    const auto install = fs::temp_directory_path() / "jojo_unknown_ps1_conversion_install";
    std::error_code ec;
    fs::remove(source, ec);
    fs::remove_all(install, ec);
    test_iso::write_image(source);

    const auto converted = jojo::convert_image(source, install);
    if (converted || converted.error != jojo::ErrorCode::unknown_revision) {
        std::cerr << "unknown PS1 revision must be rejected\n";
        fs::remove(source, ec);
        fs::remove_all(install, ec);
        return 1;
    }

    if (fs::exists(install / "active_install.ini")) {
        std::cerr << "unknown PS1 revision must never activate an installation\n";
        fs::remove(source, ec);
        fs::remove_all(install, ec);
        return 1;
    }

    fs::remove(source, ec);
    fs::remove_all(install, ec);
    std::cout << "unknown PS1 revision rejection contract passed\n";
    return 0;
}
