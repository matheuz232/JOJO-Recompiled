#include "core/iso9660.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace jojo {
namespace {
constexpr std::uint64_t kSectorSize = 2048;
constexpr std::uint64_t kPvdSector = 16;

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool bounded_extent(std::uint64_t source_size, std::uint32_t lba, std::uint32_t bytes) noexcept {
    if (lba > std::numeric_limits<std::uint64_t>::max() / kSectorSize) return false;
    const std::uint64_t offset = static_cast<std::uint64_t>(lba) * kSectorSize;
    return offset <= source_size && static_cast<std::uint64_t>(bytes) <= source_size - offset;
}

Result<std::vector<std::uint8_t>> read_extent(const Iso9660Image& image,
                                               std::uint32_t lba,
                                               std::uint32_t bytes) {
    if (!bounded_extent(image.source_size, lba, bytes)) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_installation, "ISO9660 extent is outside the source image");
    }
    std::ifstream in(image.source_path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error, "cannot reopen ISO9660 source image");
    }
    const auto offset = static_cast<std::uint64_t>(lba) * kSectorSize;
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::io_error, "cannot seek ISO9660 extent");
    }
    std::vector<std::uint8_t> data(bytes);
    if (bytes) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(bytes));
    if (static_cast<std::uint64_t>(in.gcount()) != bytes) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::io_error, "short read in ISO9660 extent");
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(data));
}

std::string normalize_name(std::string name) {
    const auto semicolon = name.find(';');
    if (semicolon != std::string::npos) name.resize(semicolon);
    if (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

Result<std::vector<DiscFileEntry>> parse_directory(const Iso9660Image& image,
                                                    std::uint32_t lba,
                                                    std::uint32_t size,
                                                    std::string_view parent_path) {
    auto bytes = read_extent(image, lba, size);
    if (!bytes) return Result<std::vector<DiscFileEntry>>::failure(bytes.error, bytes.detail);

    std::vector<DiscFileEntry> entries;
    std::size_t cursor = 0;
    while (cursor < bytes.value.size()) {
        const std::uint8_t record_len = bytes.value[cursor];
        if (record_len == 0) {
            const std::size_t next_sector = ((cursor / kSectorSize) + 1) * kSectorSize;
            if (next_sector <= cursor) break;
            cursor = std::min(next_sector, bytes.value.size());
            continue;
        }
        if (record_len < 34 || cursor + record_len > bytes.value.size() ||
            (cursor % kSectorSize) + record_len > kSectorSize) {
            return Result<std::vector<DiscFileEntry>>::failure(
                ErrorCode::invalid_installation, "malformed ISO9660 directory record");
        }
        const auto* record = bytes.value.data() + cursor;
        const std::uint8_t name_len = record[32];
        if (name_len == 0 || 33u + name_len > record_len) {
            return Result<std::vector<DiscFileEntry>>::failure(
                ErrorCode::invalid_installation, "malformed ISO9660 file identifier");
        }
        const bool special = name_len == 1 && (record[33] == 0 || record[33] == 1);
        if (!special) {
            std::string name(reinterpret_cast<const char*>(record + 33), name_len);
            name = normalize_name(std::move(name));
            if (name.empty()) {
                return Result<std::vector<DiscFileEntry>>::failure(
                    ErrorCode::invalid_installation, "empty ISO9660 file identifier");
            }
            const auto extent_lba = le32(record + 2);
            const auto file_size = le32(record + 10);
            if (!bounded_extent(image.source_size, extent_lba, file_size)) {
                return Result<std::vector<DiscFileEntry>>::failure(
                    ErrorCode::invalid_installation, "ISO9660 directory entry points outside the image");
            }
            std::string full = parent_path.empty() || parent_path == "/" ? "/" + name
                                                                           : std::string(parent_path) + "/" + name;
            entries.push_back({std::move(name), std::move(full), (record[25] & 0x02u) != 0,
                               extent_lba, file_size});
        }
        cursor += record_len;
    }
    return Result<std::vector<DiscFileEntry>>::success(std::move(entries));
}
}

Result<Iso9660Image> open_iso9660(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return Result<Iso9660Image>::failure(ErrorCode::file_not_found,
                                             "ISO9660 image not found: " + path.string());
    }
    const std::uint64_t pvd_offset = kPvdSector * kSectorSize;
    if (size < pvd_offset + kSectorSize) {
        return Result<Iso9660Image>::failure(ErrorCode::unsupported_format,
                                             "image is too small to contain an ISO9660 PVD");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return Result<Iso9660Image>::failure(ErrorCode::io_error, "cannot open ISO9660 image");
    in.seekg(static_cast<std::streamoff>(pvd_offset));
    std::array<std::uint8_t, kSectorSize> pvd{};
    in.read(reinterpret_cast<char*>(pvd.data()), static_cast<std::streamsize>(pvd.size()));
    if (in.gcount() != static_cast<std::streamsize>(pvd.size())) {
        return Result<Iso9660Image>::failure(ErrorCode::io_error, "short read while reading ISO9660 PVD");
    }
    if (pvd[0] != 1 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0 || pvd[6] != 1) {
        return Result<Iso9660Image>::failure(ErrorCode::unsupported_format, "invalid ISO9660 Primary Volume Descriptor");
    }
    const auto* root = pvd.data() + 156;
    if (root[0] < 34 || root[32] != 1 || root[33] != 0 || (root[25] & 0x02u) == 0) {
        return Result<Iso9660Image>::failure(ErrorCode::invalid_installation, "invalid ISO9660 root directory record");
    }
    const auto root_lba = le32(root + 2);
    const auto root_size = le32(root + 10);
    if (!bounded_extent(size, root_lba, root_size)) {
        return Result<Iso9660Image>::failure(ErrorCode::invalid_installation, "ISO9660 root directory is outside the image");
    }
    return Result<Iso9660Image>::success(Iso9660Image{path, size, root_lba, root_size});
}

namespace {
std::string ascii_upper(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

Result<std::vector<std::string>> split_virtual_path(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') ++start;
        if (start == path.size()) break;
        const auto slash = path.find('/', start);
        const auto end = slash == std::string_view::npos ? path.size() : slash;
        std::string part(path.substr(start, end - start));
        if (part == "..") {
            return Result<std::vector<std::string>>::failure(
                ErrorCode::invalid_argument, "parent traversal is not allowed in ISO9660 paths");
        }
        if (!part.empty() && part != ".") parts.push_back(std::move(part));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return Result<std::vector<std::string>>::success(std::move(parts));
}

Result<DiscFileEntry> find_entry(const Iso9660Image& image, std::string_view virtual_path) {
    auto parts = split_virtual_path(virtual_path);
    if (!parts) return Result<DiscFileEntry>::failure(parts.error, parts.detail);
    if (parts.value.empty()) {
        return Result<DiscFileEntry>::failure(ErrorCode::invalid_argument, "path refers to ISO9660 root");
    }

    std::uint32_t lba = image.root_extent_lba;
    std::uint32_t size = image.root_size_bytes;
    std::string parent = "/";
    DiscFileEntry found{};
    for (std::size_t i = 0; i < parts.value.size(); ++i) {
        auto entries = parse_directory(image, lba, size, parent);
        if (!entries) return Result<DiscFileEntry>::failure(entries.error, entries.detail);
        const auto wanted = ascii_upper(parts.value[i]);
        const auto it = std::find_if(entries.value.begin(), entries.value.end(), [&](const DiscFileEntry& entry) {
            return ascii_upper(entry.name) == wanted;
        });
        if (it == entries.value.end()) {
            return Result<DiscFileEntry>::failure(ErrorCode::file_not_found,
                                                   "ISO9660 path not found: " + std::string(virtual_path));
        }
        found = *it;
        if (i + 1 < parts.value.size()) {
            if (!found.is_directory) {
                return Result<DiscFileEntry>::failure(ErrorCode::invalid_argument,
                                                       "ISO9660 path component is not a directory: " + found.path);
            }
            lba = found.extent_lba;
            size = found.size_bytes;
            parent = found.path;
        }
    }
    return Result<DiscFileEntry>::success(std::move(found));
}
}

Result<std::vector<DiscFileEntry>> list_iso9660_directory(const Iso9660Image& image,
                                                          std::string_view virtual_path) {
    if (virtual_path.empty() || virtual_path == "/") {
        return parse_directory(image, image.root_extent_lba, image.root_size_bytes, "/");
    }
    auto entry = find_entry(image, virtual_path);
    if (!entry) return Result<std::vector<DiscFileEntry>>::failure(entry.error, entry.detail);
    if (!entry.value.is_directory) {
        return Result<std::vector<DiscFileEntry>>::failure(ErrorCode::invalid_argument,
                                                            "ISO9660 path is not a directory: " + entry.value.path);
    }
    return parse_directory(image, entry.value.extent_lba, entry.value.size_bytes, entry.value.path);
}

Result<std::vector<std::uint8_t>> read_iso9660_file(const Iso9660Image& image,
                                                    std::string_view virtual_path) {
    auto entry = find_entry(image, virtual_path);
    if (!entry) return Result<std::vector<std::uint8_t>>::failure(entry.error, entry.detail);
    if (entry.value.is_directory) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument,
                                                          "ISO9660 path is a directory: " + entry.value.path);
    }
    return read_extent(image, entry.value.extent_lba, entry.value.size_bytes);
}

}
