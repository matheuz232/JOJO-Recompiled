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

inline std::filesystem::path write_image(const std::filesystem::path& path) {
    std::vector<std::uint8_t> image(24 * sector, 0);
    const std::size_t pvd = 16 * sector;
    image[pvd] = 1;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(pvd + 1));
    image[pvd + 6] = 1;
    dir_record(image, pvd + 156, 20, static_cast<std::uint32_t>(sector), 2, {}, 0);

    const std::size_t term = 17 * sector;
    image[term] = 255;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(term + 1));
    image[term + 6] = 1;

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
}
