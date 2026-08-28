#include "core/dreamcast_boot.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace jojo {
namespace {
constexpr std::uint32_t kSystemAreaSectors = 16;

std::string fixed_ascii(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset,
                        std::size_t width) {
    if (offset > bytes.size() || width > bytes.size() - offset) return {};
    std::string value(reinterpret_cast<const char*>(bytes.data() + offset), width);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\0')) value.pop_back();
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\0')) ++first;
    if (first == value.size()) return {};
    if (first != 0) value.erase(0, first);
    return value;
}

bool safe_boot_filename(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return name.find("..") == std::string::npos;
}

std::string ascii_upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& data) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::array<char, 16> result{};
    result.fill('0');
    std::array<char, 16> scratch{};
    const auto [end, ec] = std::to_chars(scratch.data(), scratch.data() + scratch.size(), value, 16);
    if (ec != std::errc{}) return {};
    const auto count = static_cast<std::size_t>(end - scratch.data());
    std::copy_n(scratch.data(), count, result.data() + (result.size() - count));
    return std::string(result.data(), result.size());
}
}

Result<DreamcastIpMetadata> read_dreamcast_ip_metadata(const Iso9660Image& image) {
    auto system_area = read_logical_sectors(image.sectors, 0, kSystemAreaSectors);
    if (!system_area) {
        return Result<DreamcastIpMetadata>::failure(system_area.error, system_area.detail);
    }
    if (system_area.value.size() < 256) {
        return Result<DreamcastIpMetadata>::failure(
            ErrorCode::invalid_installation, "Dreamcast system area is too small for IP metadata");
    }

    DreamcastIpMetadata metadata{};
    metadata.hardware_id = fixed_ascii(system_area.value, 0x000, 16);
    metadata.maker_id = fixed_ascii(system_area.value, 0x010, 16);
    metadata.device_info = fixed_ascii(system_area.value, 0x020, 16);
    metadata.area_symbols = fixed_ascii(system_area.value, 0x030, 8);
    metadata.peripherals = fixed_ascii(system_area.value, 0x038, 8);
    metadata.product_number = fixed_ascii(system_area.value, 0x040, 10);
    metadata.product_version = fixed_ascii(system_area.value, 0x04A, 6);
    metadata.release_field = fixed_ascii(system_area.value, 0x050, 16);
    metadata.boot_filename = fixed_ascii(system_area.value, 0x060, 16);
    metadata.company_name = fixed_ascii(system_area.value, 0x070, 16);
    metadata.software_name = fixed_ascii(system_area.value, 0x080, 128);

    if (metadata.hardware_id != "SEGA SEGAKATANA") {
        return Result<DreamcastIpMetadata>::failure(
            ErrorCode::unsupported_format, "system area does not contain a Dreamcast IP header");
    }
    if (!safe_boot_filename(metadata.boot_filename)) {
        return Result<DreamcastIpMetadata>::failure(
            ErrorCode::invalid_argument, "Dreamcast boot filename is empty or unsafe");
    }
    return Result<DreamcastIpMetadata>::success(std::move(metadata));
}

Result<DreamcastBootProgram> read_dreamcast_boot_program(const Iso9660Image& image,
                                                         std::uint64_t max_program_bytes) {
    auto metadata = read_dreamcast_ip_metadata(image);
    if (!metadata) {
        return Result<DreamcastBootProgram>::failure(metadata.error, metadata.detail);
    }
    if (max_program_bytes == 0) {
        return Result<DreamcastBootProgram>::failure(ErrorCode::invalid_argument,
                                                     "boot program size limit must be non-zero");
    }

    auto root = list_iso9660_directory(image, "/");
    if (!root) return Result<DreamcastBootProgram>::failure(root.error, root.detail);
    const auto wanted = ascii_upper(metadata.value.boot_filename);
    const auto it = std::find_if(root.value.begin(), root.value.end(), [&](const DiscFileEntry& entry) {
        return !entry.is_directory && ascii_upper(entry.name) == wanted;
    });
    if (it == root.value.end()) {
        return Result<DreamcastBootProgram>::failure(
            ErrorCode::file_not_found, "Dreamcast boot program was not found in the disc root");
    }
    if (it->size_bytes == 0 || static_cast<std::uint64_t>(it->size_bytes) > max_program_bytes) {
        return Result<DreamcastBootProgram>::failure(
            ErrorCode::invalid_installation, "Dreamcast boot program is empty or exceeds the configured size limit");
    }

    auto bytes = read_iso9660_file(image, "/" + it->name);
    if (!bytes) return Result<DreamcastBootProgram>::failure(bytes.error, bytes.detail);
    const auto hash = fnv1a64(bytes.value);
    DreamcastBootProgram program{std::move(metadata.value), std::move(bytes.value), hash, hex64(hash)};
    return Result<DreamcastBootProgram>::success(std::move(program));
}

}
