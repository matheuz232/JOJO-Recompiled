#include "core/replay.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace jojo {

namespace {

constexpr std::size_t max_replay_id_size = 128;
constexpr std::size_t max_mod_hash_size = 128;
constexpr std::size_t max_frame_count = 1'000'000;

bool valid_mode(OnlineMode mode) noexcept {
    switch (mode) {
        case OnlineMode::casual:
        case OnlineMode::ranked:
        case OnlineMode::direct:
        case OnlineMode::custom:
            return true;
    }
    return false;
}

bool valid_hash(std::string_view hash) noexcept {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_string(std::vector<std::uint8_t>& out, std::string_view value) {
    append_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] bool read_u8(std::uint8_t& value) noexcept {
        if (offset_ + 1 > bytes_.size()) return false;
        value = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool read_u16(std::uint16_t& value) noexcept {
        if (offset_ + 2 > bytes_.size()) return false;
        value = static_cast<std::uint16_t>(bytes_[offset_])
            | (static_cast<std::uint16_t>(bytes_[offset_ + 1]) << 8);
        offset_ += 2;
        return true;
    }

    [[nodiscard]] bool read_u32(std::uint32_t& value) noexcept {
        if (offset_ + 4 > bytes_.size()) return false;
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool read_u64(std::uint64_t& value) noexcept {
        if (offset_ + 8 > bytes_.size()) return false;
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool read_string(std::string& value, std::size_t max_size) {
        std::uint16_t size{};
        if (!read_u16(size)) return false;
        if (size > max_size || offset_ + size > bytes_.size()) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + static_cast<std::ptrdiff_t>(offset_)),
            size);
        offset_ += size;
        return true;
    }

    [[nodiscard]] bool read_fixed_string(std::string& value, std::size_t size) {
        if (offset_ + size > bytes_.size()) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + static_cast<std::ptrdiff_t>(offset_)),
            size);
        offset_ += size;
        return true;
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

void append_input(std::vector<std::uint8_t>& out, const RollbackInput& input) {
    append_u32(out, input.buttons);
    append_u16(out, std::bit_cast<std::uint16_t>(input.axis_x));
    append_u16(out, std::bit_cast<std::uint16_t>(input.axis_y));
}

bool read_input(Reader& reader, RollbackInput& input) {
    std::uint32_t buttons{};
    std::uint16_t axis_x{};
    std::uint16_t axis_y{};
    if (!reader.read_u32(buttons) || !reader.read_u16(axis_x) || !reader.read_u16(axis_y)) {
        return false;
    }
    input.buttons = buttons;
    input.axis_x = std::bit_cast<std::int16_t>(axis_x);
    input.axis_y = std::bit_cast<std::int16_t>(axis_y);
    return true;
}

} // namespace

Result<void> validate_online_replay(const OnlineReplay& replay) {
    if (replay.replay_id.empty() || replay.replay_id.size() > max_replay_id_size) {
        return Result<void>::failure(ErrorCode::invalid_argument, "replay id must contain 1..128 bytes");
    }
    if (!valid_mode(replay.mode)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "replay mode is invalid");
    }
    if (!valid_hash(replay.initial_state_hash_hex)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "initial replay state hash must be 64 hexadecimal characters");
    }
    if (replay.mod_set_hash.size() > max_mod_hash_size) {
        return Result<void>::failure(ErrorCode::invalid_argument, "replay mod-set hash exceeds 128 bytes");
    }
    if (replay.frames.size() > max_frame_count) {
        return Result<void>::failure(ErrorCode::invalid_argument, "replay frame count exceeds 1000000");
    }

    bool first = true;
    std::uint64_t previous{};
    for (const auto& frame : replay.frames) {
        if (!valid_hash(frame.state_hash_hex)) {
            return Result<void>::failure(ErrorCode::invalid_argument, "replay frame hash must be 64 hexadecimal characters");
        }
        if (!first && frame.frame <= previous) {
            return Result<void>::failure(ErrorCode::invalid_argument, "replay frame indexes must be strictly increasing");
        }
        first = false;
        previous = frame.frame;
    }
    return Result<void>::success();
}

Result<std::vector<std::uint8_t>> serialize_online_replay(const OnlineReplay& replay) {
    const auto validated = validate_online_replay(replay);
    if (!validated) {
        return Result<std::vector<std::uint8_t>>::failure(validated.error, validated.detail);
    }

    std::vector<std::uint8_t> out;
    out.reserve(32 + replay.replay_id.size() + replay.mod_set_hash.size() + replay.frames.size() * 96);
    out.push_back('J');
    out.push_back('R');
    out.push_back('P');
    out.push_back('L');
    append_u16(out, kReplayFormatVersion);
    append_u8(out, static_cast<std::uint8_t>(replay.mode));
    append_u8(out, 0);
    append_u64(out, replay.rng_seed);
    append_string(out, replay.replay_id);
    append_string(out, replay.initial_state_hash_hex);
    append_string(out, replay.mod_set_hash);
    append_u32(out, static_cast<std::uint32_t>(replay.frames.size()));

    for (const auto& frame : replay.frames) {
        append_u64(out, frame.frame);
        append_input(out, frame.local);
        append_input(out, frame.remote);
        out.insert(out.end(), frame.state_hash_hex.begin(), frame.state_hash_hex.end());
    }

    return Result<std::vector<std::uint8_t>>::success(std::move(out));
}

Result<OnlineReplay> parse_online_replay(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    std::uint8_t magic[4]{};
    for (auto& byte : magic) {
        if (!reader.read_u8(byte)) {
            return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay header is truncated");
        }
    }
    if (magic[0] != 'J' || magic[1] != 'R' || magic[2] != 'P' || magic[3] != 'L') {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay magic is invalid");
    }

    std::uint16_t version{};
    std::uint8_t mode_raw{};
    std::uint8_t reserved{};
    std::uint64_t rng_seed{};
    if (!reader.read_u16(version) || !reader.read_u8(mode_raw) || !reader.read_u8(reserved)
        || !reader.read_u64(rng_seed)) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay header is truncated");
    }
    if (version != kReplayFormatVersion) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay version is unsupported");
    }
    if (mode_raw > static_cast<std::uint8_t>(OnlineMode::custom)) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay mode is invalid");
    }
    if (reserved != 0) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay reserved header byte must be zero");
    }

    OnlineReplay replay{};
    replay.mode = static_cast<OnlineMode>(mode_raw);
    replay.rng_seed = rng_seed;
    if (!reader.read_string(replay.replay_id, max_replay_id_size)
        || !reader.read_string(replay.initial_state_hash_hex, 64)
        || !reader.read_string(replay.mod_set_hash, max_mod_hash_size)) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay string field is malformed or truncated");
    }

    std::uint32_t frame_count{};
    if (!reader.read_u32(frame_count)) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay frame count is truncated");
    }
    if (frame_count > max_frame_count) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay frame count exceeds 1000000");
    }

    replay.frames.reserve(frame_count);
    for (std::uint32_t index = 0; index < frame_count; ++index) {
        ReplayFrame frame{};
        if (!reader.read_u64(frame.frame) || !read_input(reader, frame.local) || !read_input(reader, frame.remote)
            || !reader.read_fixed_string(frame.state_hash_hex, 64)) {
            return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay frame data is truncated");
        }
        replay.frames.push_back(std::move(frame));
    }

    if (reader.offset() != reader.size()) {
        return Result<OnlineReplay>::failure(ErrorCode::unsupported_format, "replay contains trailing data");
    }

    const auto validated = validate_online_replay(replay);
    if (!validated) {
        return Result<OnlineReplay>::failure(validated.error, validated.detail);
    }
    return Result<OnlineReplay>::success(std::move(replay));
}

} // namespace jojo
