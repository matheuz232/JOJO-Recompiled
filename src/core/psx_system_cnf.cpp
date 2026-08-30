#include "core/psx_system_cnf.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace jojo {
namespace {

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

}
