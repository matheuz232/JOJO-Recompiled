#include "core/ps1_cdrom_state.h"

namespace jojo {
namespace {

constexpr std::uint32_t kCdromIndexStatus = 0x1F801800u;
constexpr std::uint32_t kCdromResponseCommand = 0x1F801801u;
constexpr std::uint32_t kCdromRequestInterrupt = 0x1F801803u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_bool(std::uint64_t& hash, bool value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value ? 1u : 0u));
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

void Ps1CdromState::seed_post_bios(std::uint8_t drive_status,
                                   std::uint8_t interrupt_enable) noexcept {
    index_ = 0u;
    drive_status_ = drive_status;
    interrupt_enable_ = static_cast<std::uint8_t>(interrupt_enable & 0x1Fu);
    interrupt_status_ = 0u;
    response_.reset();
    command_count_ = 0u;
    last_command_event_.reset();
    irq_line_ = false;
    irq_rising_edge_pending_ = false;
    recompute_irq();
}

Ps1CdromIoResult Ps1CdromState::read8(std::uint32_t physical) noexcept {
    if (physical == kCdromIndexStatus) {
        const auto result_ready = response_ ? 0x20u : 0u;
        return {Ps1CdromIoStatus::ok,
                static_cast<std::uint8_t>(0x18u | result_ready | (index_ & 3u))};
    }
    if (physical == kCdromRequestInterrupt && index_ == 1u) {
        return {Ps1CdromIoStatus::ok,
                static_cast<std::uint8_t>(0xE0u | (interrupt_status_ & 0x1Fu))};
    }
    if (physical == kCdromResponseCommand && index_ == 0u && response_) {
        const auto value = *response_;
        response_.reset();
        return {Ps1CdromIoStatus::ok, value};
    }
    return {};
}

Ps1CdromIoResult Ps1CdromState::write8(std::uint32_t physical,
                                       std::uint8_t value) noexcept {
    if (physical == kCdromIndexStatus) {
        index_ = static_cast<std::uint8_t>(value & 3u);
        return {Ps1CdromIoStatus::ok, 0u};
    }
    if (physical == kCdromRequestInterrupt) {
        if (index_ == 0u && value == 0u) {
            return {Ps1CdromIoStatus::ok, 0u};
        }
        return {};
    }
    if (physical == kCdromResponseCommand && index_ == 0u) {
        if (value != 0x01u || response_) {
            return {Ps1CdromIoStatus::unsupported_command, 0u};
        }
        response_ = drive_status_;
        interrupt_status_ = 3u;
        ++command_count_;
        last_command_event_ = Ps1CdromCommandEvent{
            command_count_, 0x01u, index_, drive_status_};
        recompute_irq();
        return {Ps1CdromIoStatus::ok, 0u};
    }
    return {};
}

void Ps1CdromState::recompute_irq() noexcept {
    const bool next = (interrupt_enable_ & interrupt_status_ & 0x1Fu) != 0u;
    if (!irq_line_ && next) irq_rising_edge_pending_ = true;
    irq_line_ = next;
}

bool Ps1CdromState::take_irq_rising_edge() noexcept {
    const bool pending = irq_rising_edge_pending_;
    irq_rising_edge_pending_ = false;
    return pending;
}

bool Ps1CdromState::irq_line() const noexcept { return irq_line_; }
std::uint64_t Ps1CdromState::command_count() const noexcept { return command_count_; }
const std::optional<Ps1CdromCommandEvent>& Ps1CdromState::last_command_event() const noexcept {
    return last_command_event_;
}
std::uint8_t Ps1CdromState::index() const noexcept { return index_; }
std::uint8_t Ps1CdromState::drive_status() const noexcept { return drive_status_; }
std::uint8_t Ps1CdromState::interrupt_enable() const noexcept { return interrupt_enable_; }
std::uint8_t Ps1CdromState::interrupt_status() const noexcept { return interrupt_status_; }

std::uint64_t Ps1CdromState::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_byte(hash, index_);
    hash_byte(hash, drive_status_);
    hash_byte(hash, interrupt_enable_);
    hash_byte(hash, interrupt_status_);
    hash_bool(hash, response_.has_value());
    if (response_) hash_byte(hash, *response_);
    hash_u64(hash, command_count_);
    hash_bool(hash, last_command_event_.has_value());
    if (last_command_event_) {
        hash_u64(hash, last_command_event_->sequence);
        hash_byte(hash, last_command_event_->command);
        hash_byte(hash, last_command_event_->index);
        hash_byte(hash, last_command_event_->status);
    }
    hash_bool(hash, irq_line_);
    hash_bool(hash, irq_rising_edge_pending_);
    return hash;
}

} // namespace jojo