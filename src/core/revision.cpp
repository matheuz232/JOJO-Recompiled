#include "core/revision.h"
#include "core/psx_exe.h"
#include "core/psx_system_cnf.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>

namespace jojo {
namespace {

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& data) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

std::string ascii_upper(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return out;
}

Result<std::uint32_t> parse_hex_u32(std::string_view value, std::string_view key) {
    value = trim(value);
    if (value.empty()) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                              std::string(key) + " value is empty");
    }
    std::uint64_t parsed{};
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed, 16);
    if (ec != std::errc{} || ptr != end || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                              std::string(key) + " is not a valid 32-bit hexadecimal value");
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed));
}

Result<std::string> normalize_boot_path(std::string_view value) {
    value = trim(value);
    if (value.empty()) {
        return Result<std::string>::failure(ErrorCode::invalid_installation,
                                            "SYSTEM.CNF BOOT value is empty");
    }

    const auto separator = value.find_first_of(" \t");
    if (separator != std::string_view::npos) value = value.substr(0, separator);

    constexpr std::string_view prefix = "CDROM:";
    if (value.size() < prefix.size() || ascii_upper(value.substr(0, prefix.size())) != prefix) {
        return Result<std::string>::failure(ErrorCode::invalid_installation,
                                            "SYSTEM.CNF BOOT target must use cdrom:");
    }
    value.remove_prefix(prefix.size());
    if (value.empty() || (value.front() != '\\' && value.front() != '/')) {
        return Result<std::string>::failure(ErrorCode::invalid_installation,
                                            "SYSTEM.CNF BOOT path must be absolute on cdrom");
    }

    std::string path(value);
    std::replace(path.begin(), path.end(), '\\', '/');

    const auto version = path.rfind(';');
    if (version != std::string::npos) {
        if (version + 1 >= path.size()) {
            return Result<std::string>::failure(ErrorCode::invalid_installation,
                                                "SYSTEM.CNF BOOT ISO version is empty");
        }
        for (std::size_t i = version + 1; i < path.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(path[i]))) {
                return Result<std::string>::failure(ErrorCode::invalid_installation,
                                                    "SYSTEM.CNF BOOT ISO version is invalid");
            }
        }
        path.resize(version);
    }

    if (path.empty() || path == "/") {
        return Result<std::string>::failure(ErrorCode::invalid_installation,
                                            "SYSTEM.CNF BOOT path does not name an executable");
    }

    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') ++start;
        if (start >= path.size()) break;
        const auto slash = path.find('/', start);
        const auto end = slash == std::string::npos ? path.size() : slash;
        const auto component = std::string_view(path).substr(start, end - start);
        if (component == "." || component == "..") {
            return Result<std::string>::failure(ErrorCode::invalid_installation,
                                                "SYSTEM.CNF BOOT path contains traversal components");
        }
        start = slash == std::string::npos ? path.size() : slash + 1;
    }

    return Result<std::string>::success(std::move(path));
}

std::uint32_t read_le32(std::span<const std::uint8_t> file, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(file[offset]) |
           (static_cast<std::uint32_t>(file[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(file[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(file[offset + 3u]) << 24u);
}

}

Result<PsxSystemCnf> parse_psx_system_cnf(std::string_view text) {
    if (text.find('\0') != std::string_view::npos) {
        return Result<PsxSystemCnf>::failure(ErrorCode::invalid_installation,
                                             "SYSTEM.CNF contains embedded NUL bytes");
    }

    PsxSystemCnf result{};
    bool have_boot = false;
    bool have_tcb = false;
    bool have_event = false;
    bool have_stack = false;

    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const auto newline = text.find('\n', cursor);
        auto line = text.substr(cursor, newline == std::string_view::npos ? text.size() - cursor
                                                                          : newline - cursor);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = trim(line);
        if (!line.empty()) {
            const auto eq = line.find('=');
            if (eq != std::string_view::npos) {
                const auto key = ascii_upper(trim(line.substr(0, eq)));
                const auto value = trim(line.substr(eq + 1));
                if (key == "BOOT") {
                    if (have_boot) {
                        return Result<PsxSystemCnf>::failure(ErrorCode::invalid_installation,
                                                             "SYSTEM.CNF contains duplicate BOOT entries");
                    }
                    auto parsed = normalize_boot_path(value);
                    if (!parsed) return Result<PsxSystemCnf>::failure(parsed.error, parsed.detail);
                    result.boot_iso_path = std::move(parsed.value);
                    have_boot = true;
                } else if (key == "TCB") {
                    if (have_tcb) {
                        return Result<PsxSystemCnf>::failure(ErrorCode::invalid_installation,
                                                             "SYSTEM.CNF contains duplicate TCB entries");
                    }
                    auto parsed = parse_hex_u32(value, "TCB");
                    if (!parsed) return Result<PsxSystemCnf>::failure(parsed.error, parsed.detail);
                    result.tcb = parsed.value;
                    have_tcb = true;
                } else if (key == "EVENT") {
                    if (have_event) {
                        return Result<PsxSystemCnf>::failure(ErrorCode::invalid_installation,
                                                             "SYSTEM.CNF contains duplicate EVENT entries");
                    }
                    auto parsed = parse_hex_u32(value, "EVENT");
                    if (!parsed) return Result<PsxSystemCnf>::failure(parsed.error, parsed.detail);
                    result.event = parsed.value;
                    have_event = true;
                } else if (key == "STACK") {
                    if (have_stack) {
                        return Result<PsxSystemCnf>::failure(ErrorCode::invalid_installation,
                                                             "SYSTEM.CNF contains duplicate STACK entries");
                    }
                    auto parsed = parse_hex_u32(value, "STACK");
                    if (!parsed) return Result<PsxSystemCnf>::failure(parsed.error, parsed.detail);
                    result.stack = parsed.value;
                    have_stack = true;
                }
            }
        }
        if (newline == std::string_view::npos) break;
        cursor = newline + 1;
    }

    if (!have_boot) {
        return Result<PsxSystemCnf>::failure(ErrorCode::invalid_installation,
                                             "SYSTEM.CNF is missing BOOT entry");
    }
    return Result<PsxSystemCnf>::success(std::move(result));
}

Result<PsxExeHeader> parse_psx_exe(std::span<const std::uint8_t> file) {
    constexpr std::size_t header_size = 0x800u;
    constexpr char magic[] = "PS-X EXE";

    if (file.size() < header_size) {
        return Result<PsxExeHeader>::failure(ErrorCode::invalid_installation,
                                             "PS-X EXE file is shorter than its 0x800-byte header");
    }
    for (std::size_t i = 0; i < sizeof(magic) - 1u; ++i) {
        if (file[i] != static_cast<std::uint8_t>(magic[i])) {
            return Result<PsxExeHeader>::failure(ErrorCode::invalid_installation,
                                                 "PlayStation executable is missing PS-X EXE magic");
        }
    }

    PsxExeHeader header{};
    header.initial_pc = read_le32(file, 0x10u);
    header.initial_gp = read_le32(file, 0x14u);
    header.load_address = read_le32(file, 0x18u);
    header.payload_size = read_le32(file, 0x1cu);
    header.data_start = read_le32(file, 0x20u);
    header.data_size = read_le32(file, 0x24u);
    header.bss_start = read_le32(file, 0x28u);
    header.bss_size = read_le32(file, 0x2cu);
    header.stack_base = read_le32(file, 0x30u);
    header.stack_offset = read_le32(file, 0x34u);

    if ((header.payload_size % 0x800u) != 0u) {
        return Result<PsxExeHeader>::failure(ErrorCode::invalid_installation,
                                             "PS-X EXE payload size is not 0x800-byte aligned");
    }
    if (file.size() - header_size != static_cast<std::size_t>(header.payload_size)) {
        return Result<PsxExeHeader>::failure(ErrorCode::invalid_installation,
                                             "PS-X EXE payload size does not match file size");
    }

    const auto load_begin = static_cast<std::uint64_t>(header.load_address);
    const auto load_end = load_begin + static_cast<std::uint64_t>(header.payload_size);
    constexpr std::uint64_t address_space_end = 0x1'0000'0000ull;
    if (load_end > address_space_end) {
        return Result<PsxExeHeader>::failure(ErrorCode::invalid_installation,
                                             "PS-X EXE load range overflows the 32-bit address space");
    }
    const auto pc = static_cast<std::uint64_t>(header.initial_pc);
    if (pc < load_begin || pc >= load_end) {
        return Result<PsxExeHeader>::failure(ErrorCode::invalid_installation,
                                             "PS-X EXE initial PC is outside the loaded payload");
    }

    return Result<PsxExeHeader>::success(header);
}

Result<GameRevisionMatch> identify_game_revision(
    const Iso9660Image& image,
    const std::vector<GameRevisionProfile>& profiles) {
    if (profiles.empty()) {
        return Result<GameRevisionMatch>::failure(
            ErrorCode::unknown_revision,
            "no verified revision profiles are registered yet for this game");
    }
    std::string first_mismatch;
    for (const auto& profile : profiles) {
        if (profile.revision_id.empty() || profile.files.empty()) continue;
        bool matches = true;
        for (const auto& expected : profile.files) {
            auto file = read_iso9660_file(image, expected.path);
            if (!file) {
                if (file.error == ErrorCode::file_not_found) {
                    if (first_mismatch.empty()) {
                        first_mismatch = "profile '" + profile.revision_id + "' is missing " + expected.path;
                    }
                    matches = false;
                    break;
                }
                return Result<GameRevisionMatch>::failure(file.error, file.detail);
            }
            if (file.value.size() != expected.size_bytes || fnv1a64(file.value) != expected.fnv1a64) {
                if (first_mismatch.empty()) {
                    first_mismatch = "profile '" + profile.revision_id + "' fingerprint mismatch for " + expected.path;
                }
                matches = false;
                break;
            }
        }
        if (matches) {
            return Result<GameRevisionMatch>::success(GameRevisionMatch{profile.revision_id});
        }
    }
    std::string detail = "disc image does not match any supported game revision profile";
    if (!first_mismatch.empty()) detail += "; " + first_mismatch;
    return Result<GameRevisionMatch>::failure(ErrorCode::unknown_revision, std::move(detail));
}

}
