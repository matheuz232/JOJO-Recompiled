#include "core/ps1_omega_summary.h"

#include "core/ps1_omega_session_io.h"
#include "core/sha256.h"
#include "core/version.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace jojo {
namespace {

struct ParsedSegment {
    bool strict{};
    std::uint64_t epoch{};
    std::uint64_t state_hash{};
    std::string stop_reason;
    std::uint64_t retired{};
    std::uint32_t last_pc{};
    std::uint64_t gp0{};
    std::uint64_t gp1{};
    std::uint64_t dma{};
    std::uint64_t vram{};
    std::uint64_t frames{};
    std::optional<std::uint32_t> mmio_address;
    std::uint8_t mmio_width{};
    bool mmio_write{};
    std::uint32_t mmio_value{};
    std::string raw;
};

struct CoverageUnion {
    std::set<std::uint32_t> pcs;
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    std::set<std::tuple<std::uint32_t, std::uint8_t, bool>> mmio;
};

struct FrameFirstSummary {
    std::optional<std::uint64_t> strict_gp0;
    std::optional<std::uint64_t> speculative_gp0;
    std::optional<std::uint64_t> strict_dma;
    std::optional<std::uint64_t> speculative_dma;
    std::optional<std::uint64_t> strict_vram;
    std::optional<std::uint64_t> speculative_vram;
    std::optional<std::uint64_t> strict_presentable;
    std::optional<std::uint64_t> speculative_presentable;
    std::optional<std::uint64_t> strict_commercial;
    std::optional<std::uint64_t> speculative_commercial;
};

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 4u > bytes.size()) return 0u;
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 8u > bytes.size()) return 0u;
    std::uint64_t value{};
    for (unsigned i = 0u; i < 8u; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8u);
    }
    return value;
}

Result<std::vector<std::uint8_t>> read_verified_payload(
    const std::filesystem::path& root,
    const Ps1OmegaChunkRecord& record) {
    const auto path = root / record.relative_path;
    const auto whole_digest = sha256_file(path);
    if (!whole_digest || sha256_hex(whole_digest.value) != record.sha256) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error, "OMEGA evidence chunk SHA-256 mismatch: " + path.string());
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error, "failed to open OMEGA evidence chunk: " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() != kPs1OmegaChunkHeaderBytes + record.payload_bytes) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error, "OMEGA evidence chunk size mismatch: " + path.string());
    }

    static constexpr std::array<std::uint8_t, 8> magic{
        'J', 'O', 'J', 'O', 'O', 'M', 'E', 'G',
    };
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()) ||
        read_u32(bytes, 8u) != kPs1OmegaSessionSchemaVersion ||
        bytes[12u] != static_cast<std::uint8_t>(record.category) ||
        read_u64(bytes, 13u) != record.sequence ||
        read_u64(bytes, 21u) != record.epoch ||
        read_u64(bytes, 29u) != record.payload_bytes) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error, "OMEGA evidence chunk header mismatch: " + path.string());
    }

    std::vector<std::uint8_t> payload(
        bytes.begin() + static_cast<std::ptrdiff_t>(kPs1OmegaChunkHeaderBytes), bytes.end());
    const auto payload_digest = sha256(payload);
    if (!std::equal(payload_digest.begin(), payload_digest.end(), bytes.begin() + 37)) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error, "OMEGA evidence payload SHA-256 mismatch: " + path.string());
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(payload));
}

std::map<std::string, std::string> parse_key_values(std::string_view text) {
    std::map<std::string, std::string> values;
    std::size_t start{};
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        const auto equal = line.find('=');
        if (equal != std::string_view::npos) {
            values[std::string(line.substr(0, equal))] = std::string(line.substr(equal + 1u));
        }
        if (end == std::string_view::npos) break;
        start = end + 1u;
    }
    return values;
}

std::optional<std::uint64_t> parse_integer(std::string_view text) {
    try {
        std::size_t used{};
        const bool hex = text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        const auto value = std::stoull(std::string(text), &used, hex ? 16 : 10);
        if (used != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::uint64_t value_u64(const std::map<std::string, std::string>& values,
                        std::string_view key,
                        std::uint64_t fallback = 0u) {
    const auto found = values.find(std::string(key));
    if (found == values.end()) return fallback;
    const auto parsed = parse_integer(found->second);
    return parsed.value_or(fallback);
}

std::string value_text(const std::map<std::string, std::string>& values,
                       std::string_view key) {
    const auto found = values.find(std::string(key));
    return found == values.end() ? std::string{} : found->second;
}

ParsedSegment parse_segment(std::string raw, std::uint64_t epoch) {
    const auto values = parse_key_values(raw);
    ParsedSegment segment{};
    segment.raw = std::move(raw);
    segment.epoch = epoch;
    segment.strict = value_text(values, "evidence") == "strict";
    segment.state_hash = value_u64(values, "state_hash");
    segment.stop_reason = value_text(values, "stop_reason");
    segment.retired = value_u64(values, "instructions_retired");
    segment.last_pc = static_cast<std::uint32_t>(value_u64(values, "last_pc"));
    segment.gp0 = value_u64(values, "gpu_gp0_command_count");
    segment.gp1 = value_u64(values, "gpu_gp1_command_count");
    segment.dma = value_u64(values, "dma_transfer_count");
    segment.vram = value_u64(values, "vram_write_count");
    segment.frames = value_u64(values, "presented_frames");
    if (const auto address = values.find("mmio_last_address"); address != values.end()) {
        if (const auto parsed = parse_integer(address->second); parsed) {
            segment.mmio_address = static_cast<std::uint32_t>(*parsed);
        }
    }
    segment.mmio_width = static_cast<std::uint8_t>(value_u64(values, "mmio_last_width"));
    segment.mmio_write = value_u64(values, "mmio_last_write") != 0u;
    segment.mmio_value = static_cast<std::uint32_t>(value_u64(values, "mmio_last_value"));
    return segment;
}

class CoverageReader {
public:
    explicit CoverageReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        if (cursor_ >= bytes_.size()) { ok_ = false; return 0u; }
        return bytes_[cursor_++];
    }
    std::uint32_t u32() {
        std::uint32_t value{};
        for (unsigned i = 0u; i < 4u; ++i) value |= static_cast<std::uint32_t>(u8()) << (i * 8u);
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value{};
        for (unsigned i = 0u; i < 8u; ++i) value |= static_cast<std::uint64_t>(u8()) << (i * 8u);
        return value;
    }
    bool ok() const noexcept { return ok_; }
    bool finished() const noexcept { return ok_ && cursor_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_{};
    bool ok_{true};
};

bool merge_coverage_payload(std::span<const std::uint8_t> payload,
                            CoverageUnion& coverage) {
    CoverageReader reader(payload);
    if (reader.u32() != 1u) return false;
    const auto pc_count = reader.u64();
    if (pc_count > 100000000ull) return false;
    for (std::uint64_t i = 0; i < pc_count; ++i) coverage.pcs.insert(reader.u32());
    const auto edge_count = reader.u64();
    if (edge_count > 100000000ull) return false;
    for (std::uint64_t i = 0; i < edge_count; ++i) {
        coverage.edges.insert({reader.u32(), reader.u32()});
    }
    const auto mmio_count = reader.u64();
    if (mmio_count > 100000000ull) return false;
    for (std::uint64_t i = 0; i < mmio_count; ++i) {
        coverage.mmio.insert({reader.u32(), reader.u8(), reader.u8() != 0u});
    }
    return reader.finished();
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

std::string blocker_description(const ParsedSegment& segment) {
    std::ostringstream out;
    out << "stop=" << segment.stop_reason << ",pc=" << hex32(segment.last_pc);
    if (segment.mmio_address) {
        out << ",address=" << hex32(*segment.mmio_address)
            << ",width=" << static_cast<unsigned>(segment.mmio_width)
            << ",write=" << (segment.mmio_write ? 1 : 0)
            << ",value=" << hex32(segment.mmio_value);
    }
    return out.str();
}

bool terminal_segment(const ParsedSegment& segment) {
    return !segment.stop_reason.empty() && segment.stop_reason != "execution_budget_exhausted";
}

using SegmentScore = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t,
                                std::uint64_t, std::uint64_t, std::uint64_t,
                                std::uint64_t>;

SegmentScore segment_score(const ParsedSegment& segment) {
    return {segment.frames, segment.vram, segment.gp0, segment.gp1,
            segment.dma, segment.retired, segment.epoch};
}

void record_first(std::optional<std::uint64_t>& target,
                  std::uint64_t retired) {
    if (!target) target = retired;
}

std::string optional_number(const std::optional<std::uint64_t>& value) {
    return value ? std::to_string(*value) : "none";
}

std::string category_name(Ps1OmegaEvidenceCategory category) {
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

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20u) {
                out << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                    << static_cast<unsigned>(ch) << std::dec;
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
}

Result<void> write_atomic_text(const std::filesystem::path& path,
                               std::string_view text) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return Result<void>::failure(ErrorCode::io_error, ec.message());
    }
    auto temporary = path;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
    ec.clear();
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return Result<void>::failure(ErrorCode::io_error, "failed to open bundle temporary file");
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) return Result<void>::failure(ErrorCode::io_error, "failed to flush bundle temporary file");
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) return Result<void>::failure(ErrorCode::io_error, "failed to commit bundle file: " + ec.message());
    return Result<void>::success();
}

void put_u16(std::ostream& out, std::uint16_t value) {
    const char bytes[2]{static_cast<char>(value), static_cast<char>(value >> 8u)};
    out.write(bytes, 2);
}

void put_u32(std::ostream& out, std::uint32_t value) {
    char bytes[4]{};
    for (unsigned i = 0; i < 4u; ++i) bytes[i] = static_cast<char>(value >> (i * 8u));
    out.write(bytes, 4);
}

void put_u64(std::ostream& out, std::uint64_t value) {
    char bytes[8]{};
    for (unsigned i = 0; i < 8u; ++i) bytes[i] = static_cast<char>(value >> (i * 8u));
    out.write(bytes, 8);
}

std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::uint8_t> bytes) {
    std::uint32_t value = crc;
    for (const auto byte : bytes) {
        value ^= byte;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            value = (value >> 1u) ^ (0xEDB88320u & (0u - (value & 1u)));
        }
    }
    return value;
}

struct ZipEntry {
    std::filesystem::path absolute;
    std::string relative;
    std::uint64_t size{};
    std::uint32_t crc{};
    std::uint64_t local_offset{};
};

Result<void> measure_zip_entry(ZipEntry& entry,
                               const std::function<bool()>& cancel) {
    std::ifstream in(entry.absolute, std::ios::binary);
    if (!in) return Result<void>::failure(ErrorCode::io_error, "failed to open ZIP source file");
    std::array<std::uint8_t, 1024u * 1024u> buffer{};
    std::uint32_t crc = 0xFFFFFFFFu;
    std::uint64_t size{};
    while (in) {
        if (cancel && cancel()) return Result<void>::failure(ErrorCode::io_error, "OMEGA bundle packaging cancelled");
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) {
            crc = crc32_update(crc, std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(count)));
            size += static_cast<std::uint64_t>(count);
        }
    }
    if (in.bad()) return Result<void>::failure(ErrorCode::io_error, "failed while reading ZIP source file");
    entry.size = size;
    entry.crc = ~crc;
    return Result<void>::success();
}

Result<void> copy_file_to_stream(const std::filesystem::path& path,
                                 std::ostream& out,
                                 const std::function<bool()>& cancel) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Result<void>::failure(ErrorCode::io_error, "failed to reopen ZIP source file");
    std::array<char, 1024u * 1024u> buffer{};
    while (in) {
        if (cancel && cancel()) return Result<void>::failure(ErrorCode::io_error, "OMEGA bundle packaging cancelled");
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) out.write(buffer.data(), count);
        if (!out) return Result<void>::failure(ErrorCode::io_error, "failed while writing ZIP payload");
    }
    if (in.bad()) return Result<void>::failure(ErrorCode::io_error, "failed while reading ZIP payload");
    return Result<void>::success();
}

} // namespace

std::string ps1_omega_infinity_stop_reason_name(
    Ps1OmegaInfinityStopReason reason) noexcept {
    switch (reason) {
    case Ps1OmegaInfinityStopReason::none: return "none";
    case Ps1OmegaInfinityStopReason::user_requested: return "user_requested";
    case Ps1OmegaInfinityStopReason::strict_commercial_frame: return "strict_commercial_frame";
    case Ps1OmegaInfinityStopReason::fatal_no_safe_continuation: return "fatal_no_safe_continuation";
    case Ps1OmegaInfinityStopReason::disk_budget_exhausted: return "disk_budget_exhausted";
    case Ps1OmegaInfinityStopReason::invalid_resume_state: return "invalid_resume_state";
    }
    return "unknown";
}

Result<Ps1OmegaBundleInfo> finalize_ps1_omega_bundle(
    const std::filesystem::path& session_root,
    std::string_view executable_identity,
    const Ps1OmegaInfinitySummary& run_summary) {
    auto manifest_result = load_ps1_omega_session_manifest(session_root);
    if (!manifest_result) {
        return Result<Ps1OmegaBundleInfo>::failure(manifest_result.error, manifest_result.detail);
    }
    const auto& manifest = manifest_result.value;

    std::vector<ParsedSegment> segments;
    CoverageUnion coverage;
    std::set<std::uint64_t> state_hashes;
    for (const auto& chunk : manifest.chunks) {
        auto payload = read_verified_payload(session_root, chunk);
        if (!payload) {
            return Result<Ps1OmegaBundleInfo>::failure(payload.error, payload.detail);
        }
        if (chunk.category == Ps1OmegaEvidenceCategory::cpu) {
            std::string raw(payload.value.begin(), payload.value.end());
            auto segment = parse_segment(std::move(raw), chunk.epoch);
            state_hashes.insert(segment.state_hash);
            segments.push_back(std::move(segment));
        } else if (chunk.category == Ps1OmegaEvidenceCategory::coverage) {
            if (!merge_coverage_payload(payload.value, coverage)) {
                return Result<Ps1OmegaBundleInfo>::failure(
                    ErrorCode::invalid_settings, "invalid OMEGA coverage chunk");
            }
        }
    }

    std::set<std::string> strict_blockers;
    std::set<std::string> speculative_blockers;
    FrameFirstSummary frame{};
    std::uint64_t strict_retired{};
    std::uint64_t speculative_retired{};
    const ParsedSegment* best_strict = nullptr;
    for (const auto& segment : segments) {
        auto& cumulative = segment.strict ? strict_retired : speculative_retired;
        cumulative += segment.retired;
        if (segment.gp0 != 0u) record_first(segment.strict ? frame.strict_gp0 : frame.speculative_gp0, cumulative);
        if (segment.dma != 0u) record_first(segment.strict ? frame.strict_dma : frame.speculative_dma, cumulative);
        if (segment.vram != 0u) record_first(segment.strict ? frame.strict_vram : frame.speculative_vram, cumulative);
        if (segment.frames != 0u) record_first(segment.strict ? frame.strict_presentable : frame.speculative_presentable, cumulative);
        if (segment.stop_reason == "commercial_frame_presented") {
            record_first(segment.strict ? frame.strict_commercial : frame.speculative_commercial, cumulative);
        }
        if (terminal_segment(segment)) {
            (segment.strict ? strict_blockers : speculative_blockers).insert(blocker_description(segment));
        }
        if (segment.strict && (!best_strict || segment_score(segment) > segment_score(*best_strict))) {
            best_strict = &segment;
        }
    }

    std::ostringstream summary;
    summary << "format=jojo-omega-infinity-summary-v1\n";
    summary << "schema_version=" << kPs1OmegaSessionSchemaVersion << '\n';
    summary << "core_version=" << core_version() << '\n';
    summary << "executable_identity=" << executable_identity << '\n';
    summary << "epoch_count=" << run_summary.epoch_count << '\n';
    summary << "total_retired=" << run_summary.total_retired << '\n';
    summary << "committed_evidence_bytes=" << manifest.committed_bytes << '\n';
    summary << "chunk_count=" << manifest.chunks.size() << '\n';
    summary << "strict_frontier_count=" << strict_blockers.size() << '\n';
    summary << "speculative_frontier_count=" << speculative_blockers.size() << '\n';
    summary << "unique_serialized_state_count=" << state_hashes.size() << '\n';
    summary << "coverage_unique_pc_count=" << coverage.pcs.size() << '\n';
    summary << "coverage_unique_edge_count=" << coverage.edges.size() << '\n';
    summary << "coverage_unique_mmio_count=" << coverage.mmio.size() << '\n';
    summary << "presented_frames=" << run_summary.presented_frames << '\n';
    summary << "stop_reason=" << ps1_omega_infinity_stop_reason_name(run_summary.stop_reason) << '\n';
    summary << "frame_first_strict_first_gp0=" << optional_number(frame.strict_gp0) << '\n';
    summary << "frame_first_speculative_first_gp0=" << optional_number(frame.speculative_gp0) << '\n';
    summary << "frame_first_strict_first_dma_to_gpu=" << optional_number(frame.strict_dma) << '\n';
    summary << "frame_first_speculative_first_dma_to_gpu=" << optional_number(frame.speculative_dma) << '\n';
    summary << "frame_first_strict_first_vram_write=" << optional_number(frame.strict_vram) << '\n';
    summary << "frame_first_speculative_first_vram_write=" << optional_number(frame.speculative_vram) << '\n';
    summary << "frame_first_strict_first_valid_display_config=unavailable_not_serialized\n";
    summary << "frame_first_speculative_first_valid_display_config=unavailable_not_serialized\n";
    summary << "frame_first_strict_first_presentable_framebuffer=" << optional_number(frame.strict_presentable) << '\n';
    summary << "frame_first_speculative_first_presentable_framebuffer=" << optional_number(frame.speculative_presentable) << '\n';
    summary << "frame_first_strict_commercial_frame=" << optional_number(frame.strict_commercial) << '\n';
    summary << "frame_first_speculative_commercial_frame=" << optional_number(frame.speculative_commercial) << '\n';
    for (const auto& omission : manifest.not_serialized) {
        summary << "not_serialized_" << omission << "=1\n";
    }
    summary << "not_serialized_valid_display_config_landmark=1\n";
    summary << "strict_blocker_count=" << strict_blockers.size() << '\n';
    std::size_t index{};
    for (const auto& blocker : strict_blockers) summary << "strict_blocker_" << index++ << '=' << blocker << '\n';
    summary << "speculative_future_blocker_count=" << speculative_blockers.size() << '\n';
    index = 0u;
    for (const auto& blocker : speculative_blockers) summary << "speculative_future_blocker_" << index++ << '=' << blocker << '\n';
    summary << "strict_best_report_begin=1\n";
    if (best_strict) summary << best_strict->raw;
    if (best_strict && !best_strict->raw.empty() && best_strict->raw.back() != '\n') summary << '\n';
    summary << "strict_best_report_end=1\n";

    std::ostringstream json;
    json << "{\n";
    json << "  \"format\": \"jojo-omega-infinity-bundle-v1\",\n";
    json << "  \"schema_version\": " << kPs1OmegaSessionSchemaVersion << ",\n";
    json << "  \"core_version\": \"" << json_escape(core_version()) << "\",\n";
    json << "  \"executable_identity\": \"" << json_escape(executable_identity) << "\",\n";
    json << "  \"summary\": \"summary.txt\",\n";
    json << "  \"stop_reason\": \"" << ps1_omega_infinity_stop_reason_name(run_summary.stop_reason) << "\",\n";
    json << "  \"epoch_count\": " << run_summary.epoch_count << ",\n";
    json << "  \"total_retired\": " << run_summary.total_retired << ",\n";
    json << "  \"committed_evidence_bytes\": " << manifest.committed_bytes << ",\n";
    json << "  \"not_serialized\": [";
    for (std::size_t i = 0u; i < manifest.not_serialized.size(); ++i) {
        if (i != 0u) json << ", ";
        json << '"' << json_escape(manifest.not_serialized[i]) << '"';
    }
    json << "],\n";
    json << "  \"chunks\": [\n";
    for (std::size_t i = 0u; i < manifest.chunks.size(); ++i) {
        const auto& chunk = manifest.chunks[i];
        json << "    {\"sequence\": " << chunk.sequence
             << ", \"epoch\": " << chunk.epoch
             << ", \"category\": \"" << category_name(chunk.category)
             << "\", \"payload_bytes\": " << chunk.payload_bytes
             << ", \"sha256\": \"" << chunk.sha256
             << "\", \"path\": \"" << json_escape(chunk.relative_path.generic_string()) << "\"}";
        if (i + 1u != manifest.chunks.size()) json << ',';
        json << '\n';
    }
    json << "  ]\n";
    json << "}\n";

    const auto summary_path = session_root / "summary.txt";
    const auto manifest_path = session_root / "manifest.json";
    auto saved_summary = write_atomic_text(summary_path, summary.str());
    if (!saved_summary) return Result<Ps1OmegaBundleInfo>::failure(saved_summary.error, saved_summary.detail);
    auto saved_manifest = write_atomic_text(manifest_path, json.str());
    if (!saved_manifest) return Result<Ps1OmegaBundleInfo>::failure(saved_manifest.error, saved_manifest.detail);

    Ps1OmegaBundleInfo info{};
    info.summary_path = summary_path;
    info.manifest_path = manifest_path;
    info.strict_frontier_count = strict_blockers.size();
    info.speculative_frontier_count = speculative_blockers.size();
    info.unique_serialized_state_count = state_hashes.size();
    info.coverage_unique_pc_count = coverage.pcs.size();
    info.coverage_unique_edge_count = coverage.edges.size();
    info.coverage_unique_mmio_count = coverage.mmio.size();
    return Result<Ps1OmegaBundleInfo>::success(std::move(info));
}

Result<std::filesystem::path> package_ps1_omega_bundle_zip(
    const std::filesystem::path& session_root,
    const std::filesystem::path& output_zip,
    std::function<bool()> cancel) {
    std::error_code ec;
    if (!std::filesystem::is_directory(session_root, ec) || ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::invalid_argument, "OMEGA session root is not a directory");
    }

    std::vector<ZipEntry> entries;
    auto temporary = output_zip;
    temporary += ".tmp";
    for (std::filesystem::recursive_directory_iterator it(session_root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        const auto absolute = it->path();
        if (absolute == output_zip || absolute == temporary) continue;
        const auto relative = std::filesystem::relative(absolute, session_root, ec);
        if (ec) break;
        entries.push_back(ZipEntry{absolute, relative.generic_string()});
    }
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error, "failed to enumerate OMEGA bundle: " + ec.message());
    }
    std::sort(entries.begin(), entries.end(), [](const ZipEntry& a, const ZipEntry& b) {
        return a.relative < b.relative;
    });

    for (auto& entry : entries) {
        auto measured = measure_zip_entry(entry, cancel);
        if (!measured) return Result<std::filesystem::path>::failure(measured.error, measured.detail);
    }

    if (!output_zip.parent_path().empty()) {
        std::filesystem::create_directories(output_zip.parent_path(), ec);
        if (ec) return Result<std::filesystem::path>::failure(ErrorCode::io_error, ec.message());
    }
    std::filesystem::remove(temporary, ec);
    ec.clear();
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return Result<std::filesystem::path>::failure(ErrorCode::io_error, "failed to create OMEGA ZIP temporary file");

    for (auto& entry : entries) {
        if (cancel && cancel()) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return Result<std::filesystem::path>::failure(ErrorCode::io_error, "OMEGA bundle packaging cancelled");
        }
        entry.local_offset = static_cast<std::uint64_t>(out.tellp());
        put_u32(out, 0x04034B50u);
        put_u16(out, 45u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u32(out, entry.crc);
        put_u32(out, 0xFFFFFFFFu);
        put_u32(out, 0xFFFFFFFFu);
        put_u16(out, static_cast<std::uint16_t>(entry.relative.size()));
        put_u16(out, 20u);
        out.write(entry.relative.data(), static_cast<std::streamsize>(entry.relative.size()));
        put_u16(out, 0x0001u);
        put_u16(out, 16u);
        put_u64(out, entry.size);
        put_u64(out, entry.size);
        auto copied = copy_file_to_stream(entry.absolute, out, cancel);
        if (!copied) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return Result<std::filesystem::path>::failure(copied.error, copied.detail);
        }
    }

    const auto central_offset = static_cast<std::uint64_t>(out.tellp());
    for (const auto& entry : entries) {
        put_u32(out, 0x02014B50u);
        put_u16(out, 45u);
        put_u16(out, 45u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u32(out, entry.crc);
        put_u32(out, 0xFFFFFFFFu);
        put_u32(out, 0xFFFFFFFFu);
        put_u16(out, static_cast<std::uint16_t>(entry.relative.size()));
        put_u16(out, 28u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u16(out, 0u);
        put_u32(out, 0u);
        put_u32(out, 0xFFFFFFFFu);
        out.write(entry.relative.data(), static_cast<std::streamsize>(entry.relative.size()));
        put_u16(out, 0x0001u);
        put_u16(out, 24u);
        put_u64(out, entry.size);
        put_u64(out, entry.size);
        put_u64(out, entry.local_offset);
    }
    const auto central_end = static_cast<std::uint64_t>(out.tellp());
    const auto central_size = central_end - central_offset;
    const auto zip64_eocd_offset = central_end;

    put_u32(out, 0x06064B50u);
    put_u64(out, 44u);
    put_u16(out, 45u);
    put_u16(out, 45u);
    put_u32(out, 0u);
    put_u32(out, 0u);
    put_u64(out, entries.size());
    put_u64(out, entries.size());
    put_u64(out, central_size);
    put_u64(out, central_offset);

    put_u32(out, 0x07064B50u);
    put_u32(out, 0u);
    put_u64(out, zip64_eocd_offset);
    put_u32(out, 1u);

    put_u32(out, 0x06054B50u);
    put_u16(out, 0u);
    put_u16(out, 0u);
    put_u16(out, 0xFFFFu);
    put_u16(out, 0xFFFFu);
    put_u32(out, 0xFFFFFFFFu);
    put_u32(out, 0xFFFFFFFFu);
    put_u16(out, 0u);
    out.flush();
    if (!out) {
        out.close();
        std::filesystem::remove(temporary, ec);
        return Result<std::filesystem::path>::failure(ErrorCode::io_error, "failed to flush OMEGA Zip64 bundle");
    }
    out.close();

    std::filesystem::remove(output_zip, ec);
    ec.clear();
    std::filesystem::rename(temporary, output_zip, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error, "failed to commit OMEGA Zip64 bundle: " + ec.message());
    }
    return Result<std::filesystem::path>::success(output_zip);
}

} // namespace jojo