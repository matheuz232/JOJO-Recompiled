#include "core/iso9660.h"
#include "core/psx_boot.h"
#include "core/psx_revision.h"
#include "core/revision.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: jojo_r1_media_probe <cue|bin|iso>\n";
        return 2;
    }

    const auto image = jojo::open_iso9660(argv[1]);
    if (!image) {
        std::cerr << "open_error=" << image.detail << '\n';
        return 3;
    }

    const auto boot = jojo::analyze_psx_boot(image.value);
    if (!boot) {
        std::cerr << "boot_error=" << boot.detail << '\n';
        return 4;
    }

    const auto revision = jojo::identify_game_revision(
        image.value, jojo::supported_psx_game_revision_profiles());
    if (!revision) {
        std::cerr << "revision_error=" << revision.detail << '\n';
        return 5;
    }

    std::cout << "revision=" << revision.value.revision_id << '\n';
    std::cout << "boot=" << boot.value.executable_path << '\n';
    std::cout << std::hex << std::showbase;
    std::cout << "pc=" << boot.value.executable.initial_pc << '\n';
    std::cout << "load=" << boot.value.executable.load_address << '\n';
    std::cout << "payload=" << boot.value.executable.payload_size << '\n';
    std::cout << "stack=" << boot.value.executable.stack_base << '\n';
    return 0;
}
