#include "core/ps1_gpu_state.h"

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value));
    hash_byte(hash, static_cast<std::uint8_t>(value >> 8u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

void Ps1GpuState::reset_control_state() noexcept {
    display_disabled_ = true;
    dma_direction_ = 0u;
    display_vram_x_ = 0u;
    display_vram_y_ = 0u;
    horizontal_start_ = 0x0200u;
    horizontal_end_ = 0x0C00u;
    vertical_start_ = 0x0010u;
    vertical_end_ = 0x0100u;
    display_mode_ = 0u;
    irq1_ = false;
    gp0_read_latch_ = 0u;
}

bool Ps1GpuState::write_gp0(std::uint32_t value) noexcept {
    const auto command = static_cast<std::uint8_t>(value >> 24u);
    switch (command) {
        case 0x00u: // NOP
            break;
        default:
            return false;
    }
    ++gp0_command_count_;
    return true;
}

std::uint32_t Ps1GpuState::read_gp0() const noexcept {
    return gp0_read_latch_;
}

bool Ps1GpuState::write_gp1(std::uint32_t value) noexcept {
    const auto command = static_cast<std::uint8_t>(value >> 24u);
    const auto parameter = value & 0x00FFFFFFu;

    switch (command) {
        case 0x00u:
            reset_control_state();
            ++command_buffer_reset_count_;
            break;
        case 0x01u:
            ++command_buffer_reset_count_;
            break;
        case 0x02u:
            irq1_ = false;
            break;
        case 0x03u:
            display_disabled_ = (parameter & 1u) != 0u;
            break;
        case 0x04u:
            dma_direction_ = static_cast<std::uint8_t>(parameter & 3u);
            break;
        case 0x05u:
            display_vram_x_ = static_cast<std::uint16_t>(parameter & 0x03FFu);
            display_vram_y_ = static_cast<std::uint16_t>((parameter >> 10u) & 0x01FFu);
            break;
        case 0x06u:
            horizontal_start_ = static_cast<std::uint16_t>(parameter & 0x0FFFu);
            horizontal_end_ = static_cast<std::uint16_t>((parameter >> 12u) & 0x0FFFu);
            break;
        case 0x07u:
            vertical_start_ = static_cast<std::uint16_t>(parameter & 0x03FFu);
            vertical_end_ = static_cast<std::uint16_t>((parameter >> 10u) & 0x03FFu);
            break;
        case 0x08u:
            display_mode_ = static_cast<std::uint8_t>(parameter & 0xFFu);
            break;
        case 0x10u:
            if ((parameter & 0xFFu) != 7u || (parameter & 0xFFFF00u) != 0u) {
                return false;
            }
            gp0_read_latch_ = 2u; // Deterministic v2 GPU profile.
            break;
        default:
            return false;
    }

    ++gp1_command_count_;
    return true;
}

std::uint32_t Ps1GpuState::gpu_stat() const noexcept {
    // Control-oriented GPUSTAT. Full drawing/VRAM status is intentionally not modeled yet.
    std::uint32_t stat = (1u << 13) | (1u << 26) | (1u << 28);
    if (display_disabled_) stat |= 1u << 23;
    if (irq1_) stat |= 1u << 24;

    stat |= static_cast<std::uint32_t>(display_mode_ & 0x03u) << 17u;
    stat |= static_cast<std::uint32_t>((display_mode_ >> 2u) & 0x01u) << 19u;
    stat |= static_cast<std::uint32_t>((display_mode_ >> 3u) & 0x01u) << 20u;
    stat |= static_cast<std::uint32_t>((display_mode_ >> 4u) & 0x01u) << 21u;
    stat |= static_cast<std::uint32_t>((display_mode_ >> 5u) & 0x01u) << 22u;
    stat |= static_cast<std::uint32_t>((display_mode_ >> 6u) & 0x01u) << 16u;
    stat |= static_cast<std::uint32_t>((display_mode_ >> 7u) & 0x01u) << 14u;

    stat |= static_cast<std::uint32_t>(dma_direction_ & 3u) << 29u;
    bool dma_request = false;
    switch (dma_direction_ & 3u) {
        case 1u: dma_request = true; break;
        case 2u: dma_request = true; break;
        case 3u: dma_request = false; break;
        default: break;
    }
    if (dma_request) stat |= 1u << 25;
    return stat;
}

std::uint64_t Ps1GpuState::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_byte(hash, static_cast<std::uint8_t>(display_disabled_));
    hash_byte(hash, dma_direction_);
    hash_u16(hash, display_vram_x_);
    hash_u16(hash, display_vram_y_);
    hash_u16(hash, horizontal_start_);
    hash_u16(hash, horizontal_end_);
    hash_u16(hash, vertical_start_);
    hash_u16(hash, vertical_end_);
    hash_byte(hash, display_mode_);
    hash_byte(hash, static_cast<std::uint8_t>(irq1_));
    hash_u32(hash, gp0_read_latch_);
    hash_u64(hash, gp0_command_count_);
    hash_u64(hash, gp1_command_count_);
    hash_u64(hash, command_buffer_reset_count_);
    return hash;
}

std::uint64_t Ps1GpuState::gp0_command_count() const noexcept { return gp0_command_count_; }
std::uint64_t Ps1GpuState::gp1_command_count() const noexcept { return gp1_command_count_; }
std::uint64_t Ps1GpuState::command_buffer_reset_count() const noexcept { return command_buffer_reset_count_; }
bool Ps1GpuState::display_disabled() const noexcept { return display_disabled_; }
std::uint8_t Ps1GpuState::dma_direction() const noexcept { return dma_direction_; }
std::uint16_t Ps1GpuState::display_vram_x() const noexcept { return display_vram_x_; }
std::uint16_t Ps1GpuState::display_vram_y() const noexcept { return display_vram_y_; }
std::uint16_t Ps1GpuState::horizontal_start() const noexcept { return horizontal_start_; }
std::uint16_t Ps1GpuState::horizontal_end() const noexcept { return horizontal_end_; }
std::uint16_t Ps1GpuState::vertical_start() const noexcept { return vertical_start_; }
std::uint16_t Ps1GpuState::vertical_end() const noexcept { return vertical_end_; }
std::uint8_t Ps1GpuState::display_mode() const noexcept { return display_mode_; }

} // namespace jojo
