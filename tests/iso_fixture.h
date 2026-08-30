#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace test_iso {
constexpr std::size_t sector = 2048;

inline void le32(std::vector<std::uint8_t>& image, std::size_t off, std::uint32_t v) {
    image[off] = static_cast<std::uint8_t>(v);
    image[off + 1] = static_cast<std::uint8_t>(v >> 8);
    image[off + 2] = static_cast<std::uint8_t>(v >> 16);
    image[off + 3] = static_cast<std::uint8_t>(v >> 24);
}
inline void be32(std::vector<std::uint8_t>& image, std::size_t off, std::uint32_t v) {
    image[off] = static_cast<std::uint8_t>(v >> 24);
    image[off + 1] = static_cast<std::uint8_t>(v >> 16);
    image[off + 2] = static_cast<std::uint8_t>(v >> 8);
    image[off + 3] = static_cast<std::uint8_t>(v);
}

inline std::size_t dir_record(std::vector<std::uint8_t>& image, std::size_t off,
                              std::uint32_t lba, std::uint32_t size,
                              std::uint8_t flags, std::string_view name,
                              int special = -1) {
    const std::size_t name_len = special >= 0 ? 1 : name.size();
    const std::size_t record_len = 33 + name_len + ((name_len % 2) == 0 ? 1 : 0);
    image[off] = static_cast<std::uint8_t>(record_len);
    image[off + 1] = 0;
    le32(image, off + 2, lba); be32(image, off + 6, lba);
    le32(image, off + 10, size); be32(image, off + 14, size);
    image[off + 25] = flags;
    image[off + 26] = 0; image[off + 27] = 0;
    image[off + 28] = 1; image[off + 29] = 0;
    image[off + 30] = 0; image[off + 31] = 1;
    image[off + 32] = static_cast<std::uint8_t>(name_len);
    if (special >= 0) image[off + 33] = static_cast<std::uint8_t>(special);
    else std::copy(name.begin(), name.end(), image.begin() + static_cast<std::ptrdiff_t>(off + 33));
    return record_len;
}

inline void write_iso9660_header(std::vector<std::uint8_t>& image, std::uint32_t root_lba) {
    const std::size_t pvd = 16 * sector;
    image[pvd] = 1;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(pvd + 1));
    image[pvd + 6] = 1;
    dir_record(image, pvd + 156, root_lba, static_cast<std::uint32_t>(sector), 2, {}, 0);

    const std::size_t term = 17 * sector;
    image[term] = 255;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(term + 1));
    image[term + 6] = 1;
}

inline std::filesystem::path write_image(const std::filesystem::path& path) {
    std::vector<std::uint8_t> image(24 * sector, 0);
    write_iso9660_header(image, 20);

    std::size_t root = 20 * sector;
    root += dir_record(image, root, 20, static_cast<std::uint32_t>(sector), 2, {}, 0);
    root += dir_record(image, root, 20, static_cast<std::uint32_t>(sector), 2, {}, 1);
    root += dir_record(image, root, 21, 12, 0, "1ST_READ.BIN;1");
    root += dir_record(image, root, 22, static_cast<std::uint32_t>(sector), 2, "DATA");

    const std::string_view boot = "HELLO-SH4!!!";
    std::copy(boot.begin(), boot.end(), image.begin() + static_cast<std::ptrdiff_t>(21 * sector));

    std::size_t data = 22 * sector;
    data += dir_record(image, data, 22, static_cast<std::uint32_t>(sector), 2, {}, 0);
    data += dir_record(image, data, 20, static_cast<std::uint32_t>(sector), 2, {}, 1);
    data += dir_record(image, data, 23, 5, 0, "ASSET.DAT;1");
    const std::string_view asset = "ABCDE";
    std::copy(asset.begin(), asset.end(), image.begin() + static_cast<std::ptrdiff_t>(23 * sector));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return path;
}

struct PsxImageOptions {
    bool include_system_cnf{true};
    bool include_boot_executable{true};
    bool valid_executable_magic{true};
    std::string_view system_cnf{
        "BOOT = cdrom:\\SLUS_010.60;1\r\n"
        "TCB = 4\r\n"
        "EVENT = 16\r\n"
        "STACK = 801fff00\r\n"};
};

inline std::filesystem::path write_psx_image(const std::filesystem::path& path,
                                             const PsxImageOptions& options = {}) {
    constexpr std::uint32_t root_lba = 20;
    constexpr std::uint32_t exe_lba = 21;
    constexpr std::uint32_t cnf_lba = 23;
    constexpr std::uint32_t exe_size = 0x1000;
    constexpr std::uint32_t payload_size = 0x800;

    std::vector<std::uint8_t> image(24 * sector, 0);
    write_iso9660_header(image, root_lba);

    const std::size_t pvd = 16 * sector;
    std::copy_n("PLAYSTATION", 11,
                image.begin() + static_cast<std::ptrdiff_t>(pvd + 8));

    std::size_t root = root_lba * sector;
    root += dir_record(image, root, root_lba, static_cast<std::uint32_t>(sector), 2, {}, 0);
    root += dir_record(image, root, root_lba, static_cast<std::uint32_t>(sector), 2, {}, 1);
    if (options.include_boot_executable) {
        root += dir_record(image, root, exe_lba, exe_size, 0, "SLUS_010.60;1");
    }
    if (options.include_system_cnf) {
        root += dir_record(image, root, cnf_lba,
                           static_cast<std::uint32_t>(options.system_cnf.size()),
                           0, "SYSTEM.CNF;1");
    }

    if (options.include_boot_executable) {
        const std::size_t exe = exe_lba * sector;
        constexpr char magic[] = "PS-X EXE";
        if (options.valid_executable_magic) {
            std::copy_n(magic, sizeof(magic) - 1,
                        image.begin() + static_cast<std::ptrdiff_t>(exe));
        } else {
            std::copy_n("BAD-X EX", 8,
                        image.begin() + static_cast<std::ptrdiff_t>(exe));
        }
        le32(image, exe + 0x10, 0x8001000cu);
        le32(image, exe + 0x14, 0x00000000u);
        le32(image, exe + 0x18, 0x80010000u);
        le32(image, exe + 0x1c, payload_size);
        le32(image, exe + 0x20, 0u);
        le32(image, exe + 0x24, 0u);
        le32(image, exe + 0x28, 0u);
        le32(image, exe + 0x2c, 0u);
        le32(image, exe + 0x30, 0x801ffff0u);
        le32(image, exe + 0x34, 0u);
    }

    if (options.include_system_cnf && !options.system_cnf.empty()) {
        std::copy(options.system_cnf.begin(), options.system_cnf.end(),
                  image.begin() + static_cast<std::ptrdiff_t>(cnf_lba * sector));
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return path;
}

inline std::filesystem::path write_raw2352_from_iso(
    const std::filesystem::path& iso_path,
    const std::filesystem::path& raw_path,
    std::uint8_t mode = 1) {
    std::ifstream in(iso_path, std::ios::binary);
    std::vector<std::uint8_t> cooked((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    const std::size_t sectors = cooked.size() / sector;
    std::vector<std::uint8_t> raw(sectors * 2352, 0);
    for (std::size_t i = 0; i < sectors; ++i) {
        const std::size_t base = i * 2352;
        raw[base] = 0x00;
        for (std::size_t j = 1; j < 11; ++j) raw[base + j] = 0xFF;
        raw[base + 11] = 0x00;
        raw[base + 15] = mode;
        const std::size_t user = mode == 2 ? 24 : 16;
        std::copy_n(cooked.begin() + static_cast<std::ptrdiff_t>(i * sector), sector,
                    raw.begin() + static_cast<std::ptrdiff_t>(base + user));
    }
    std::ofstream out(raw_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    return raw_path;
}
}