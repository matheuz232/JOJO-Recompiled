#pragma once

#include "core/result.h"
#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace jojo {

inline constexpr std::uint32_t kPs1OmegaSessionSchemaVersion = 1u;
inline constexpr std::size_t kPs1OmegaChunkHeaderBytes = 69u;

enum class Ps1OmegaEvidenceCategory : std::uint8_t {
    cpu,
    mmio,
    bios,
    irq,
    cdrom,
    dma,
    gpu,
    timer,
    search,
    snapshot,
    coverage,
    frame_first,
    error,
};

enum class Ps1OmegaSessionIoStatus : std::uint8_t {
    ok,
    disk_budget_exhausted,
    io_error,
    invalid_manifest,
};

struct Ps1OmegaChunkRecord {
    std::uint64_t sequence{};
    std::uint64_t epoch{};
    Ps1OmegaEvidenceCategory category{Ps1OmegaEvidenceCategory::cpu};
    std::uint64_t payload_bytes{};
    std::string sha256;
    std::filesystem::path relative_path;
};

struct Ps1OmegaSessionManifest {
    std::uint32_t schema_version{kPs1OmegaSessionSchemaVersion};
    std::uint64_t next_sequence{};
    std::uint64_t committed_bytes{};
    std::uint64_t epoch_count{};
    std::uint64_t total_retired{};
    std::string executable_identity;
    std::vector<Ps1OmegaChunkRecord> chunks;
    std::vector<std::string> not_serialized{
        "hle_extended_private_state",
        "main_ram_page_deltas",
        "vram_raw_deltas",
    };
};

struct Ps1OmegaAppendResult {
    Ps1OmegaSessionIoStatus status{Ps1OmegaSessionIoStatus::io_error};
    Ps1OmegaChunkRecord record{};
    std::string detail;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == Ps1OmegaSessionIoStatus::ok;
    }
};

namespace omega_session_io_detail {

inline std::string category_name(Ps1OmegaEvidenceCategory category) {
    switch (category) {
    case Ps1OmegaEvidenceCategory::cpu: return "cpu";
    case Ps1OmegaEvidenceCategory::mmio: return "mmio";
    case Ps1OmegaEvidenceCategory::bios: return "bios";
    case Ps1OmegaEvidenceCategory::irq: return "irq";
    case Ps1OmegaEvidenceCategory::cdrom: return "cdrom";
    case Ps1OmegaEvidenceCategory::dma: return "dma";
    case Ps1OmegaEvidenceCategory::gpu: return "gpu";
    case Ps1OmegaEvidenceCategory::timer: return "timer";
    case Ps1OmegaEvidenceCategory::search: return "search";
    case Ps1OmegaEvidenceCategory::snapshot: return "snapshot";
    case Ps1OmegaEvidenceCategory::coverage: return "coverage";
    case Ps1OmegaEvidenceCategory::frame_first: return "frame-first";
    case Ps1OmegaEvidenceCategory::error: return "error";
    }
    return "unknown";
}

inline bool parse_category(std::string_view text, Ps1OmegaEvidenceCategory& out) {
    for (unsigned raw = 0; raw <= static_cast<unsigned>(Ps1OmegaEvidenceCategory::error); ++raw) {
        const auto candidate = static_cast<Ps1OmegaEvidenceCategory>(raw);
        if (category_name(candidate) == text) {
            out = candidate;
            return true;
        }
    }
    return false;
}

inline std::string fixed_hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

inline void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

inline void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

inline std::vector<std::uint8_t> make_chunk_bytes(
    const Ps1OmegaChunkRecord& record,
    std::span<const std::uint8_t> payload) {
    static constexpr std::array<std::uint8_t, 8> magic{
        'J', 'O', 'J', 'O', 'O', 'M', 'E', 'G',
    };
    const auto payload_digest = sha256(payload);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kPs1OmegaChunkHeaderBytes + payload.size());
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    append_u32(bytes, kPs1OmegaSessionSchemaVersion);
    bytes.push_back(static_cast<std::uint8_t>(record.category));
    append_u64(bytes, record.sequence);
    append_u64(bytes, record.epoch);
    append_u64(bytes, static_cast<std::uint64_t>(payload.size()));
    bytes.insert(bytes.end(), payload_digest.begin(), payload_digest.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

inline Result<void> write_binary_file(const std::filesystem::path& path,
                                      std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to open OMEGA evidence temporary file");
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    out.flush();
    if (!out) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to flush OMEGA evidence temporary file");
    }
    out.close();
    if (!out) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to close OMEGA evidence temporary file");
    }
    return Result<void>::success();
}

inline std::string serialize_manifest_state(const Ps1OmegaSessionManifest& manifest) {
    std::ostringstream out;
    out << "format=jojo-omega-session-state-v1\n";
    out << "schema_version=" << manifest.schema_version << '\n';
    out << "next_sequence=" << manifest.next_sequence << '\n';
    out << "committed_bytes=" << manifest.committed_bytes << '\n';
    out << "epoch_count=" << manifest.epoch_count << '\n';
    out << "total_retired=" << manifest.total_retired << '\n';
    out << "executable_identity=" << manifest.executable_identity << '\n';
    out << "not_serialized_count=" << manifest.not_serialized.size() << '\n';
    for (std::size_t i = 0; i < manifest.not_serialized.size(); ++i) {
        out << "not_serialized_" << i << '=' << manifest.not_serialized[i] << '\n';
    }
    out << "chunk_count=" << manifest.chunks.size() << '\n';
    for (std::size_t i = 0; i < manifest.chunks.size(); ++i) {
        const auto& chunk = manifest.chunks[i];
        out << "chunk_" << i << "_sequence=" << chunk.sequence << '\n';
        out << "chunk_" << i << "_epoch=" << chunk.epoch << '\n';
        out << "chunk_" << i << "_category=" << category_name(chunk.category) << '\n';
        out << "chunk_" << i << "_payload_bytes=" << chunk.payload_bytes << '\n';
        out << "chunk_" << i << "_sha256=" << chunk.sha256 << '\n';
        out << "chunk_" << i << "_path=" << chunk.relative_path.generic_string() << '\n';
    }
    return out.str();
}

inline Result<void> write_text_file(const std::filesystem::path& path,
                                    std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to open OMEGA manifest temporary file");
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to flush OMEGA manifest temporary file");
    }
    out.close();
    return out ? Result<void>::success()
               : Result<void>::failure(ErrorCode::io_error,
                                       "failed to close OMEGA manifest temporary file");
}

inline Result<void> commit_manifest_generation(
    const std::filesystem::path& root,
    const Ps1OmegaSessionManifest& manifest) {
    const auto directory = root / "manifest";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to create OMEGA manifest directory: " + ec.message());
    }
    const auto stem = "manifest-" + fixed_hex(manifest.next_sequence);
    const auto final_path = directory / (stem + ".state");
    const auto temporary = directory / (stem + ".state.tmp");
    const auto serialized = serialize_manifest_state(manifest);
    auto written = write_text_file(temporary, serialized);
    if (!written) return written;
    std::filesystem::rename(temporary, final_path, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to commit OMEGA manifest generation: " + ec.message());
    }
    return Result<void>::success();
}

inline bool parse_u64(const std::map<std::string, std::string>& values,
                      const std::string& key,
                      std::uint64_t& out) {
    const auto found = values.find(key);
    if (found == values.end()) return false;
    try {
        std::size_t used{};
        const auto value = std::stoull(found->second, &used, 10);
        if (used != found->second.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

inline Result<Ps1OmegaSessionManifest> parse_manifest_state(
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::io_error, "failed to open OMEGA manifest generation");
    }
    std::map<std::string, std::string> values;
    std::string line;
    if (!std::getline(in, line) || line != "format=jojo-omega-session-state-v1") {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::invalid_settings, "invalid OMEGA manifest format");
    }
    while (std::getline(in, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        values.emplace(line.substr(0, equal), line.substr(equal + 1u));
    }

    Ps1OmegaSessionManifest manifest{};
    std::uint64_t value{};
    if (!parse_u64(values, "schema_version", value) ||
        value != kPs1OmegaSessionSchemaVersion) {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::invalid_settings, "unsupported OMEGA manifest schema");
    }
    manifest.schema_version = static_cast<std::uint32_t>(value);
    if (!parse_u64(values, "next_sequence", manifest.next_sequence) ||
        !parse_u64(values, "committed_bytes", manifest.committed_bytes) ||
        !parse_u64(values, "epoch_count", manifest.epoch_count) ||
        !parse_u64(values, "total_retired", manifest.total_retired)) {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::invalid_settings, "incomplete OMEGA manifest counters");
    }
    if (const auto found = values.find("executable_identity"); found != values.end()) {
        manifest.executable_identity = found->second;
    }

    std::uint64_t omitted_count{};
    if (!parse_u64(values, "not_serialized_count", omitted_count)) {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::invalid_settings, "missing OMEGA manifest omission count");
    }
    manifest.not_serialized.clear();
    for (std::uint64_t i = 0; i < omitted_count; ++i) {
        const auto found = values.find("not_serialized_" + std::to_string(i));
        if (found == values.end()) {
            return Result<Ps1OmegaSessionManifest>::failure(
                ErrorCode::invalid_settings, "incomplete OMEGA manifest omission list");
        }
        manifest.not_serialized.push_back(found->second);
    }

    std::uint64_t chunk_count{};
    if (!parse_u64(values, "chunk_count", chunk_count)) {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::invalid_settings, "missing OMEGA manifest chunk count");
    }
    manifest.chunks.clear();
    for (std::uint64_t i = 0; i < chunk_count; ++i) {
        const auto prefix = "chunk_" + std::to_string(i) + '_';
        Ps1OmegaChunkRecord chunk{};
        if (!parse_u64(values, prefix + "sequence", chunk.sequence) ||
            !parse_u64(values, prefix + "epoch", chunk.epoch) ||
            !parse_u64(values, prefix + "payload_bytes", chunk.payload_bytes)) {
            return Result<Ps1OmegaSessionManifest>::failure(
                ErrorCode::invalid_settings, "incomplete OMEGA manifest chunk record");
        }
        const auto category = values.find(prefix + "category");
        const auto digest = values.find(prefix + "sha256");
        const auto chunk_path = values.find(prefix + "path");
        if (category == values.end() || digest == values.end() || chunk_path == values.end() ||
            !parse_category(category->second, chunk.category)) {
            return Result<Ps1OmegaSessionManifest>::failure(
                ErrorCode::invalid_settings, "invalid OMEGA manifest chunk metadata");
        }
        chunk.sha256 = digest->second;
        chunk.relative_path = std::filesystem::path(chunk_path->second);
        manifest.chunks.push_back(std::move(chunk));
    }
    return Result<Ps1OmegaSessionManifest>::success(std::move(manifest));
}

} // namespace omega_session_io_detail

[[nodiscard]] inline Result<Ps1OmegaSessionManifest> load_ps1_omega_session_manifest(
    const std::filesystem::path& root) {
    const auto manifest_dir = root / "manifest";
    std::error_code ec;
    if (!std::filesystem::exists(manifest_dir, ec)) {
        if (ec) {
            return Result<Ps1OmegaSessionManifest>::failure(
                ErrorCode::io_error, "failed to inspect OMEGA manifest directory: " + ec.message());
        }
        return Result<Ps1OmegaSessionManifest>::success(Ps1OmegaSessionManifest{});
    }

    std::vector<std::filesystem::path> generations;
    for (std::filesystem::directory_iterator it(manifest_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const auto name = it->path().filename().string();
        if (name.starts_with("manifest-") && name.ends_with(".state")) {
            generations.push_back(it->path());
        }
    }
    if (ec) {
        return Result<Ps1OmegaSessionManifest>::failure(
            ErrorCode::io_error, "failed to enumerate OMEGA manifest generations: " + ec.message());
    }
    if (generations.empty()) {
        return Result<Ps1OmegaSessionManifest>::success(Ps1OmegaSessionManifest{});
    }
    std::sort(generations.begin(), generations.end());
    return omega_session_io_detail::parse_manifest_state(generations.back());
}

class Ps1OmegaEvidenceRecorder {
public:
    Ps1OmegaEvidenceRecorder(std::filesystem::path root,
                             std::uint64_t max_disk_bytes)
        : root_(std::move(root)), max_disk_bytes_(max_disk_bytes) {
        std::error_code ec;
        std::filesystem::create_directories(root_ / "events", ec);
        if (ec) {
            detail_ = "failed to create OMEGA session directory: " + ec.message();
            return;
        }
        auto loaded = load_ps1_omega_session_manifest(root_);
        if (!loaded) {
            detail_ = loaded.detail;
            return;
        }
        manifest_ = std::move(loaded.value);
        ready_ = true;
    }

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }
    [[nodiscard]] const Ps1OmegaSessionManifest& manifest() const noexcept { return manifest_; }

    [[nodiscard]] Ps1OmegaAppendResult append(Ps1OmegaEvidenceCategory category,
                                               std::uint64_t epoch,
                                               std::span<const std::uint8_t> payload) {
        if (!ready_) {
            return {Ps1OmegaSessionIoStatus::io_error, {}, detail_};
        }
        Ps1OmegaChunkRecord record{};
        record.sequence = manifest_.next_sequence;
        record.epoch = epoch;
        record.category = category;
        record.payload_bytes = static_cast<std::uint64_t>(payload.size());
        const auto category_text = omega_session_io_detail::category_name(category);
        record.relative_path = std::filesystem::path("events") /
            (category_text + '-' + omega_session_io_detail::fixed_hex(record.sequence) + ".bin");

        const auto chunk_bytes = omega_session_io_detail::make_chunk_bytes(record, payload);
        if (chunk_bytes.size() > max_disk_bytes_ ||
            manifest_.committed_bytes > max_disk_bytes_ - static_cast<std::uint64_t>(chunk_bytes.size())) {
            return {Ps1OmegaSessionIoStatus::disk_budget_exhausted, {},
                    "OMEGA session disk budget exhausted before chunk commit"};
        }
        record.sha256 = sha256_hex(sha256(chunk_bytes));

        const auto final_path = root_ / record.relative_path;
        auto temporary = final_path;
        temporary += ".tmp";
        auto written = omega_session_io_detail::write_binary_file(temporary, chunk_bytes);
        if (!written) {
            return {Ps1OmegaSessionIoStatus::io_error, {}, written.detail};
        }
        std::error_code ec;
        std::filesystem::rename(temporary, final_path, ec);
        if (ec) {
            return {Ps1OmegaSessionIoStatus::io_error, {},
                    "failed to commit OMEGA evidence chunk: " + ec.message()};
        }

        auto next = manifest_;
        next.chunks.push_back(record);
        ++next.next_sequence;
        next.epoch_count = std::max(next.epoch_count, epoch);
        next.committed_bytes += static_cast<std::uint64_t>(chunk_bytes.size());
        auto manifest_commit = omega_session_io_detail::commit_manifest_generation(root_, next);
        if (!manifest_commit) {
            return {Ps1OmegaSessionIoStatus::io_error, {}, manifest_commit.detail};
        }
        manifest_ = std::move(next);
        return {Ps1OmegaSessionIoStatus::ok, record, {}};
    }

    [[nodiscard]] bool verify_chunk(const Ps1OmegaChunkRecord& record) const {
        const auto path = root_ / record.relative_path;
        const auto digest = sha256_file(path);
        if (!digest || sha256_hex(digest.value) != record.sha256) return false;
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        return !ec && size == kPs1OmegaChunkHeaderBytes + record.payload_bytes;
    }

private:
    std::filesystem::path root_;
    std::uint64_t max_disk_bytes_{};
    Ps1OmegaSessionManifest manifest_{};
    bool ready_{};
    std::string detail_;
};

} // namespace jojo
