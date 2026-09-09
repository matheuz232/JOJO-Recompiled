#include "core/conversion.h"
#include "core/disc_image.h"
#include "core/game_backend.h"
#include "core/version.h"
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {
struct ObservedDiscRevision {
    std::string_view source_format;
    std::uint64_t source_size;
    std::string_view hash_hex;
    std::string_view revision_id;
};

constexpr std::array<ObservedDiscRevision, 1> observed_disc_revisions{{
    {"bin", 666806112ull, "b8b5dbf79cdb9fcf", "jojo-usa-observed-b8b5dbf79cdb9fcf"},
}};

constexpr std::array<std::string_view, 28> v2_required_keys{{
    "manifest_version",
    "converter_version",
    "platform",
    "game_id",
    "source_name",
    "source_format",
    "source_size",
    "source_hash_fnv1a64",
    "revision_id",
    "system_cnf_path",
    "boot_executable",
    "psx_exe_hash_fnv1a64",
    "psx_exe_entry",
    "psx_exe_load_address",
    "psx_exe_initial_gp",
    "psx_exe_text_size",
    "psx_exe_stack_base",
    "psx_exe_stack_size",
    "media_status",
    "executable_status",
    "mips_analysis_status",
    "reference_runtime_status",
    "native_codegen_status",
    "hardware_runtime_status",
    "boot_status",
    "rendering_status",
    "audio_status",
    "input_status",
}};

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

Result<std::uint64_t> parse_u64(const std::string& text) {
    std::uint64_t value{};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return Result<std::uint64_t>::failure(ErrorCode::invalid_installation,
                                              "invalid unsigned integer in manifest: " + text);
    }
    return Result<std::uint64_t>::success(value);
}

Result<std::uint32_t> parse_u32_decimal(const std::string& text) {
    auto parsed = parse_u64(text);
    if (!parsed) return Result<std::uint32_t>::failure(parsed.error, parsed.detail);
    if (parsed.value > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                              "manifest integer exceeds uint32 range");
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value));
}

Result<std::uint32_t> parse_u32_hex_address(const std::string& text) {
    if (text.size() != 10 || text[0] != '0' || text[1] != 'x') {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                              "invalid 32-bit hexadecimal address in manifest");
    }
    for (std::size_t i = 2; i < text.size(); ++i) {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                                  "invalid 32-bit hexadecimal address in manifest");
        }
    }
    std::uint32_t value{};
    const auto* begin = text.data() + 2;
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value, 16);
    if (ec != std::errc{} || ptr != end) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                              "invalid 32-bit hexadecimal address in manifest");
    }
    return Result<std::uint32_t>::success(value);
}

std::string format_hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

bool is_lower_hex_16(std::string_view text) {
    if (text.size() != 16) return false;
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool valid_status(std::string_view value) {
    return value == "pending" || value == "verified";
}

Result<void> validate_v2_manifest(const ConversionManifest& m) {
    if (m.manifest_version != "2") {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS1 manifest_version must be 2");
    }
    if (m.platform != "playstation") {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS1 manifest platform must be playstation");
    }
    if (m.converter_version.empty() || m.game_id.empty() || m.source_name.empty() ||
        m.source_format.empty() || m.source_hash_fnv1a64.empty() || m.revision_id.empty()) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS1 manifest is missing required identity fields");
    }
    if (!is_lower_hex_16(m.source_hash_fnv1a64)) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS1 source hash must be 16 lowercase hexadecimal digits");
    }

    const std::array<std::string_view, 11> statuses{{
        m.media_status,
        m.executable_status,
        m.mips_analysis_status,
        m.reference_runtime_status,
        m.native_codegen_status,
        m.hardware_runtime_status,
        m.boot_status,
        m.rendering_status,
        m.audio_status,
        m.input_status,
        m.gameplay_status,
    }};
    for (const auto status : statuses) {
        if (!valid_status(status)) {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "PS1 manifest contains an invalid verification status");
        }
    }

    if (!m.psx_exe_hash_fnv1a64.empty() && !is_lower_hex_16(m.psx_exe_hash_fnv1a64)) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS-X EXE hash must be 16 lowercase hexadecimal digits");
    }

    if (m.executable_status == "verified") {
        if (m.media_status != "verified") {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "verified executable requires verified media");
        }
        if (m.system_cnf_path.empty() || m.boot_executable.empty() ||
            m.psx_exe_hash_fnv1a64.empty() || !m.psx_exe_entry.has_value() ||
            !m.psx_exe_load_address.has_value() || !m.psx_exe_initial_gp.has_value() ||
            !m.psx_exe_text_size.has_value() || !m.psx_exe_stack_base.has_value() ||
            !m.psx_exe_stack_size.has_value()) {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "verified executable is missing PS-X EXE evidence");
        }
    }

    const std::array<std::string_view, 9> later_statuses{{
        m.mips_analysis_status,
        m.reference_runtime_status,
        m.native_codegen_status,
        m.hardware_runtime_status,
        m.boot_status,
        m.rendering_status,
        m.audio_status,
        m.input_status,
        m.gameplay_status,
    }};
    for (const auto status : later_statuses) {
        if (status == "verified" && m.executable_status != "verified") {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "later PS1 verification requires a verified executable");
        }
    }
    return Result<void>::success();
}

Result<void> validate_v1_manifest(const ConversionManifest& m) {
    if (m.manifest_version != "1" || m.converter_version.empty() || m.source_name.empty() ||
        m.source_format.empty() || m.hash_hex.empty() || m.backend.empty()) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "legacy manifest is missing required fields");
    }
    if (m.backend == "native-ready" && !has_complete_native_backend_metadata(m)) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "native-ready manifest is missing complete native backend metadata");
    }
    return Result<void>::success();
}

Result<void> replace_file(const std::filesystem::path& temp,
                          const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::io_error, "failed to replace manifest file");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to replace manifest file: " + ec.message());
    }
    return Result<void>::success();
#endif
}

using ManifestEntries = std::vector<std::pair<std::string, std::string>>;

Result<ManifestEntries> read_manifest_entries(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return Result<ManifestEntries>::failure(ErrorCode::file_not_found,
                                                "game manifest not found: " + path.string());
    }
    ManifestEntries entries;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return Result<ManifestEntries>::failure(ErrorCode::invalid_installation,
                                                    "manifest line is missing '='");
        }
        entries.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    return Result<ManifestEntries>::success(std::move(entries));
}

Result<std::string> detect_manifest_version(const ManifestEntries& entries) {
    std::optional<std::string> version;
    for (const auto& [key, value] : entries) {
        if (key != "manifest_version") continue;
        if (version.has_value()) {
            return Result<std::string>::failure(ErrorCode::invalid_installation,
                                                "duplicate manifest_version key");
        }
        version = value;
    }
    if (!version.has_value()) {
        return Result<std::string>::failure(ErrorCode::invalid_installation,
                                            "manifest_version key is missing");
    }
    return Result<std::string>::success(std::move(*version));
}

Result<ConversionManifest> load_v1_manifest(const ManifestEntries& entries) {
    ConversionManifest m{};
    m.manifest_version = "1";
    for (const auto& [key, value] : entries) {
        if (key == "manifest_version") m.manifest_version = value;
        else if (key == "converter_version") m.converter_version = value;
        else if (key == "source_name") m.source_name = value;
        else if (key == "source_format") m.source_format = value;
        else if (key == "source_size") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.source_size = parsed.value;
        } else if (key == "hash_fnv1a64") m.hash_hex = value;
        else if (key == "revision_id") m.revision_id = value;
        else if (key == "backend") m.backend = value;
        else if (key == "boot_program_hash_fnv1a64") m.boot_program_hash_hex = value;
        else if (key == "backend_abi_version") {
            auto parsed = parse_u32_decimal(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_abi_version = parsed.value;
        } else if (key == "backend_program_hash") m.backend_program_hash = value;
        else if (key == "backend_block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_block_count = parsed.value;
        } else if (key == "backend_native_block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_native_block_count = parsed.value;
        } else if (key == "backend_fallback_block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_fallback_block_count = parsed.value;
        } else if (key == "backend_native_code_bytes") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_native_code_bytes = parsed.value;
        }
    }
    auto valid = validate_v1_manifest(m);
    if (!valid) return Result<ConversionManifest>::failure(valid.error, valid.detail);
    return Result<ConversionManifest>::success(std::move(m));
}

Result<ConversionManifest> load_v2_manifest(const ManifestEntries& entries) {
    ConversionManifest m{};
    std::unordered_set<std::string> seen;

    const auto parse_optional_hex = [&](const std::string& value,
                                        std::optional<std::uint32_t>& target) -> Result<void> {
        if (value.empty()) {
            target.reset();
            return Result<void>::success();
        }
        auto parsed = parse_u32_hex_address(value);
        if (!parsed) return Result<void>::failure(parsed.error, parsed.detail);
        target = parsed.value;
        return Result<void>::success();
    };
    const auto parse_optional_dec = [&](const std::string& value,
                                        std::optional<std::uint32_t>& target) -> Result<void> {
        if (value.empty()) {
            target.reset();
            return Result<void>::success();
        }
        auto parsed = parse_u32_decimal(value);
        if (!parsed) return Result<void>::failure(parsed.error, parsed.detail);
        target = parsed.value;
        return Result<void>::success();
    };

    for (const auto& [key, value] : entries) {
        if (!seen.insert(key).second) {
            return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                                       "duplicate key in PS1 manifest v2: " + key);
        }

        if (key == "manifest_version") m.manifest_version = value;
        else if (key == "converter_version") m.converter_version = value;
        else if (key == "platform") m.platform = value;
        else if (key == "game_id") m.game_id = value;
        else if (key == "source_name") m.source_name = value;
        else if (key == "source_format") m.source_format = value;
        else if (key == "source_size") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.source_size = parsed.value;
        } else if (key == "source_hash_fnv1a64") m.source_hash_fnv1a64 = value;
        else if (key == "revision_id") m.revision_id = value;
        else if (key == "system_cnf_path") m.system_cnf_path = value;
        else if (key == "boot_executable") m.boot_executable = value;
        else if (key == "psx_exe_hash_fnv1a64") m.psx_exe_hash_fnv1a64 = value;
        else if (key == "psx_exe_entry") {
            auto parsed = parse_optional_hex(value, m.psx_exe_entry);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
        } else if (key == "psx_exe_load_address") {
            auto parsed = parse_optional_hex(value, m.psx_exe_load_address);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
        } else if (key == "psx_exe_initial_gp") {
            auto parsed = parse_optional_hex(value, m.psx_exe_initial_gp);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
        } else if (key == "psx_exe_text_size") {
            auto parsed = parse_optional_dec(value, m.psx_exe_text_size);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
        } else if (key == "psx_exe_stack_base") {
            auto parsed = parse_optional_hex(value, m.psx_exe_stack_base);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
        } else if (key == "psx_exe_stack_size") {
            auto parsed = parse_optional_dec(value, m.psx_exe_stack_size);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
        } else if (key == "media_status") m.media_status = value;
        else if (key == "executable_status") m.executable_status = value;
        else if (key == "mips_analysis_status") m.mips_analysis_status = value;
        else if (key == "reference_runtime_status") m.reference_runtime_status = value;
        else if (key == "native_codegen_status") m.native_codegen_status = value;
        else if (key == "hardware_runtime_status") m.hardware_runtime_status = value;
        else if (key == "boot_status") m.boot_status = value;
        else if (key == "rendering_status") m.rendering_status = value;
        else if (key == "audio_status") m.audio_status = value;
        else if (key == "input_status") m.input_status = value;
        else if (key == "gameplay_status") m.gameplay_status = value;
        else {
            return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                                       "unknown key in PS1 manifest v2: " + key);
        }
    }

    for (const auto required : v2_required_keys) {
        if (!seen.contains(std::string(required))) {
            return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                                       "PS1 manifest v2 is missing required key: " +
                                                           std::string(required));
        }
    }
    if (!seen.contains("gameplay_status")) {
        return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                                   "PS1 manifest v2 is missing required key: gameplay_status");
    }

    auto valid = validate_v2_manifest(m);
    if (!valid) return Result<ConversionManifest>::failure(valid.error, valid.detail);
    return Result<ConversionManifest>::success(std::move(m));
}

void write_optional_hex(std::ostream& out, std::string_view key,
                        const std::optional<std::uint32_t>& value) {
    out << key << '=';
    if (value.has_value()) out << format_hex32(*value);
    out << '\n';
}

void write_optional_dec(std::ostream& out, std::string_view key,
                        const std::optional<std::uint32_t>& value) {
    out << key << '=';
    if (value.has_value()) out << *value;
    out << '\n';
}

void write_v1_manifest(std::ostream& out, const ConversionManifest& m) {
    out << "manifest_version=" << m.manifest_version << '\n';
    out << "converter_version=" << m.converter_version << '\n';
    out << "source_name=" << m.source_name << '\n';
    out << "source_format=" << m.source_format << '\n';
    out << "source_size=" << m.source_size << '\n';
    out << "hash_fnv1a64=" << m.hash_hex << '\n';
    out << "revision_id=" << m.revision_id << '\n';
    out << "backend=" << m.backend << '\n';
    if (!m.boot_program_hash_hex.empty()) {
        out << "boot_program_hash_fnv1a64=" << m.boot_program_hash_hex << '\n';
    }
    if (m.backend_abi_version.has_value()) out << "backend_abi_version=" << *m.backend_abi_version << '\n';
    if (!m.backend_program_hash.empty()) out << "backend_program_hash=" << m.backend_program_hash << '\n';
    if (m.backend_block_count.has_value()) out << "backend_block_count=" << *m.backend_block_count << '\n';
    if (m.backend_native_block_count.has_value()) out << "backend_native_block_count=" << *m.backend_native_block_count << '\n';
    if (m.backend_fallback_block_count.has_value()) out << "backend_fallback_block_count=" << *m.backend_fallback_block_count << '\n';
    if (m.backend_native_code_bytes.has_value()) out << "backend_native_code_bytes=" << *m.backend_native_code_bytes << '\n';
}

void write_v2_manifest(std::ostream& out, const ConversionManifest& m) {
    out << "manifest_version=" << m.manifest_version << '\n';
    out << "converter_version=" << m.converter_version << '\n';
    out << "platform=" << m.platform << '\n';
    out << "game_id=" << m.game_id << '\n';
    out << "source_name=" << m.source_name << '\n';
    out << "source_format=" << m.source_format << '\n';
    out << "source_size=" << m.source_size << '\n';
    out << "source_hash_fnv1a64=" << m.source_hash_fnv1a64 << '\n';
    out << "revision_id=" << m.revision_id << '\n';
    out << "system_cnf_path=" << m.system_cnf_path << '\n';
    out << "boot_executable=" << m.boot_executable << '\n';
    out << "psx_exe_hash_fnv1a64=" << m.psx_exe_hash_fnv1a64 << '\n';
    write_optional_hex(out, "psx_exe_entry", m.psx_exe_entry);
    write_optional_hex(out, "psx_exe_load_address", m.psx_exe_load_address);
    write_optional_hex(out, "psx_exe_initial_gp", m.psx_exe_initial_gp);
    write_optional_dec(out, "psx_exe_text_size", m.psx_exe_text_size);
    write_optional_hex(out, "psx_exe_stack_base", m.psx_exe_stack_base);
    write_optional_dec(out, "psx_exe_stack_size", m.psx_exe_stack_size);
    out << "media_status=" << m.media_status << '\n';
    out << "executable_status=" << m.executable_status << '\n';
    out << "mips_analysis_status=" << m.mips_analysis_status << '\n';
    out << "reference_runtime_status=" << m.reference_runtime_status << '\n';
    out << "native_codegen_status=" << m.native_codegen_status << '\n';
    out << "hardware_runtime_status=" << m.hardware_runtime_status << '\n';
    out << "boot_status=" << m.boot_status << '\n';
    out << "rendering_status=" << m.rendering_status << '\n';
    out << "audio_status=" << m.audio_status << '\n';
    out << "input_status=" << m.input_status << '\n';
    out << "gameplay_status=" << m.gameplay_status << '\n';
}
}

bool has_complete_native_backend_metadata(const ConversionManifest& m) noexcept {
    if (m.boot_program_hash_hex.empty() ||
        !m.backend_abi_version.has_value() ||
        m.backend_program_hash.empty() ||
        !m.backend_block_count.has_value() ||
        !m.backend_native_block_count.has_value() ||
        !m.backend_fallback_block_count.has_value() ||
        !m.backend_native_code_bytes.has_value()) {
        return false;
    }
    return *m.backend_native_block_count <= *m.backend_block_count &&
           *m.backend_fallback_block_count ==
               *m.backend_block_count - *m.backend_native_block_count;
}

Result<GameRevisionMatch> identify_observed_disc_revision(
    std::string_view source_format,
    std::uint64_t source_size,
    std::string_view hash_hex) {
    for (const auto& observed : observed_disc_revisions) {
        if (observed.source_format == source_format &&
            observed.source_size == source_size &&
            observed.hash_hex == hash_hex) {
            return Result<GameRevisionMatch>::success(
                GameRevisionMatch{std::string(observed.revision_id)});
        }
    }
    return Result<GameRevisionMatch>::failure(
        ErrorCode::unknown_revision,
        "disc fingerprint does not match any observed game revision");
}

Result<void> save_conversion_manifest_atomic(const std::filesystem::path& path,
                                             const ConversionManifest& m) {
    Result<void> valid = m.manifest_version == "1" ? validate_v1_manifest(m) : validate_v2_manifest(m);
    if (!valid) return valid;

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to create manifest directory: " + ec.message());
    }
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to create temporary manifest");
        }
        if (m.manifest_version == "1") write_v1_manifest(out, m);
        else write_v2_manifest(out, m);
        out.flush();
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed while writing manifest");
        }
    }
    return replace_file(temp, path);
}

Result<ConversionManifest> load_conversion_manifest(const std::filesystem::path& path) {
    auto entries = read_manifest_entries(path);
    if (!entries) return Result<ConversionManifest>::failure(entries.error, entries.detail);
    auto version = detect_manifest_version(entries.value);
    if (!version) return Result<ConversionManifest>::failure(version.error, version.detail);
    if (version.value == "1") return load_v1_manifest(entries.value);
    if (version.value == "2") return load_v2_manifest(entries.value);
    return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                               "unsupported manifest_version: " + version.value);
}

Result<ConversionManifest> convert_image(const std::filesystem::path& source,
                                         const std::filesystem::path& install_dir,
                                         const ConversionOptions& options,
                                         const ConversionProgressCallback& on_progress) {
    const auto report = [&](ConversionStage stage, int percent,
                            std::string message_key, std::string detail) {
        if (on_progress) {
            on_progress(ConversionProgress{stage, percent, std::move(message_key), std::move(detail)});
        }
    };

    report(ConversionStage::validating_source, 0, "validate_source",
           "Validando a imagem selecionada.");
    if (install_dir.empty()) {
        return Result<ConversionManifest>::failure(ErrorCode::invalid_argument,
                                                   "installation directory cannot be empty");
    }
    if (!supported_disc_extension(source.string())) {
        return Result<ConversionManifest>::failure(ErrorCode::unsupported_format,
                                                   "unsupported game image format");
    }

    report(ConversionStage::fingerprinting_source, 15, "fingerprint_source",
           "Calculando a identificação da imagem.");
    auto fp = fingerprint_disc_image(source);
    if (!fp) return Result<ConversionManifest>::failure(fp.error, fp.detail);

    report(ConversionStage::discovering_filesystem, 30, "discover_filesystem",
           "Lendo o sistema de arquivos da mídia em modo somente leitura.");
    auto filesystem = open_iso9660(source);
    if (!filesystem) {
        return Result<ConversionManifest>::failure(filesystem.error, filesystem.detail);
    }

    report(ConversionStage::identifying_revision, 45, "identify_revision",
           "Identificando a revisão exata do jogo.");
    auto revision = identify_game_revision(filesystem.value, options.revision_profiles);
    if (!revision && revision.error == ErrorCode::unknown_revision) {
        auto observed = identify_observed_disc_revision(
            fp.value.format, fp.value.size_bytes, fp.value.hash_hex);
        if (observed) {
            revision = std::move(observed);
            report(ConversionStage::identifying_revision, 45, "revision_observed",
                   "Revisão observada reconhecida pelo fingerprint completo da imagem.");
        }
    }
    if (!revision) {
        const bool may_prepare_unverified =
            options.allow_unverified_base_conversion &&
            options.revision_profiles.empty() &&
            revision.error == ErrorCode::unknown_revision;
        if (!may_prepare_unverified) {
            return Result<ConversionManifest>::failure(revision.error, revision.detail);
        }
        revision = Result<GameRevisionMatch>::success(
            GameRevisionMatch{"unverified-fnv1a64-" + fp.value.hash_hex});
        report(ConversionStage::identifying_revision, 45, "revision_unverified",
               "Revisão ainda não verificada; continuando somente com a preparação base.");
    }

    report(ConversionStage::preparing_installation, 55, "prepare_installation",
           "Preparando os diretórios da instalação convertida.");
    std::error_code ec;
    std::filesystem::create_directories(install_dir / "data", ec);
    if (ec) return Result<ConversionManifest>::failure(ErrorCode::io_error, ec.message());
    std::filesystem::create_directories(install_dir / "cache", ec);
    if (ec) return Result<ConversionManifest>::failure(ErrorCode::io_error, ec.message());
    std::filesystem::create_directories(install_dir / "logs", ec);
    if (ec) return Result<ConversionManifest>::failure(ErrorCode::io_error, ec.message());

    ConversionManifest manifest{};
    // Temporary bridge only: the pre-PS1 converter remains explicitly v1 until
    // Task 6 replaces this path with SYSTEM.CNF -> PS-X EXE evidence.
    manifest.manifest_version = "1";
    manifest.converter_version = core_version();
    manifest.source_name = source.filename().string();
    manifest.source_format = fp.value.format;
    manifest.source_size = fp.value.size_bytes;
    manifest.hash_hex = fp.value.hash_hex;
    manifest.revision_id = revision.value.revision_id;

    report(ConversionStage::writing_manifest, 55, "write_pending_manifest",
           "Gravando o estado pendente antes de preparar o backend específico do jogo.");
    auto saved = save_conversion_manifest_atomic(install_dir / "game_manifest.ini", manifest);
    if (!saved) return Result<ConversionManifest>::failure(saved.error, saved.detail);

    if (!supports_game_native_backend(manifest.revision_id)) {
        report(ConversionStage::completed, 100, "conversion_complete",
               "Preparação base concluída; o backend específico do jogo ainda será adicionado.");
        return Result<ConversionManifest>::success(std::move(manifest));
    }

    GameBackendProgressCallback backend_progress = [&](GameBackendStage stage) {
        switch (stage) {
            case GameBackendStage::boot_analyzed:
                report(ConversionStage::preparing_game_backend, 65,
                       "analyze_game_boot",
                       "Programa de boot Dreamcast analisado para a revisão reconhecida.");
                break;
            case GameBackendStage::cache_ready:
                report(ConversionStage::building_native_backend, 80,
                       "build_native_backend",
                       "Backend nativo gerado ou reutilizado para o programa identificado.");
                break;
            case GameBackendStage::cache_verified:
                report(ConversionStage::verifying_native_backend, 92,
                       "verify_native_backend",
                       "Cache do backend nativo recarregado e verificado.");
                break;
        }
    };

    auto prepared = prepare_game_native_backend(
        manifest.revision_id, filesystem.value, install_dir, backend_progress);
    if (!prepared) {
        return Result<ConversionManifest>::failure(prepared.error, prepared.detail);
    }

    manifest.boot_program_hash_hex = prepared.value.boot_program_hash_hex;
    manifest.backend_abi_version = prepared.value.abi_version;
    manifest.backend_program_hash = prepared.value.program_hash;
    manifest.backend_block_count = prepared.value.block_count;
    manifest.backend_native_block_count = prepared.value.native_block_count;
    manifest.backend_fallback_block_count = prepared.value.fallback_block_count;
    manifest.backend_native_code_bytes = prepared.value.native_code_bytes;
    manifest.backend = "native-ready";

    report(ConversionStage::promoting_native_backend, 97, "promote_native_backend",
           "Promovendo a instalação após verificar o backend nativo.");
    saved = save_conversion_manifest_atomic(install_dir / "game_manifest.ini", manifest);
    if (!saved) return Result<ConversionManifest>::failure(saved.error, saved.detail);

    report(ConversionStage::completed, 100, "conversion_complete",
           "Backend nativo da revisão reconhecida preparado; validação fim a fim é o próximo marco.");
    return Result<ConversionManifest>::success(std::move(manifest));
}

Result<ConversionManifest> convert_image(const std::filesystem::path& source,
                                         const std::filesystem::path& install_dir,
                                         const ConversionProgressCallback& on_progress) {
    ConversionOptions options{};
    options.allow_unverified_base_conversion = true;
    return convert_image(source, install_dir, options, on_progress);
}

}
