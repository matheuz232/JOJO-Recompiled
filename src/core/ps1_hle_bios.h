#pragma once

#include "core/r3000a_state.h"

#include <array>
#include <cstdint>
#include <optional>

namespace jojo {

class Ps1MemoryBus;

enum class Ps1HleBiosDomain : std::uint8_t {
    a0,
    b0,
    c0,
    sys,
};

struct Ps1HleBiosCall {
    Ps1HleBiosDomain domain{};
    std::uint32_t selector{};
    std::uint32_t pc{};
    std::uint32_t a0{};
    std::uint32_t a1{};
    std::uint32_t a2{};
    std::uint32_t a3{};
    std::uint32_t ra{};
};

enum class Ps1HleBiosDisposition : std::uint8_t {
    handled,
    unsupported,
    terminal,
};

struct Ps1HleBiosResult {
    Ps1HleBiosDisposition disposition{Ps1HleBiosDisposition::unsupported};
};

struct Ps1BiosHeapState {
    std::uint32_t base{};
    std::uint32_t size{};
};

class Ps1HleBios {
public:
    [[nodiscard]] Ps1HleBiosResult dispatch(
        const Ps1HleBiosCall& call,
        R3000aState& cpu) noexcept;

    [[nodiscard]] Ps1HleBiosResult dispatch(
        const Ps1HleBiosCall& call,
        R3000aState& cpu,
        Ps1MemoryBus& bus) noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(
        std::uint32_t counter) const noexcept;
    [[nodiscard]] bool iso9660_removed() const noexcept;

private:
    [[nodiscard]] Ps1HleBiosResult dispatch_impl(
        const Ps1HleBiosCall& call,
        R3000aState& cpu,
        Ps1MemoryBus* bus) noexcept;

    std::optional<Ps1BiosHeapState> heap_state_{};
    std::optional<std::uint32_t> interrupt_hook_address_{};
    std::optional<bool> pad_card_auto_ack_enabled_{};
    std::array<std::optional<bool>, 4> root_counter_auto_ack_enabled_{};
    bool iso9660_removed_{};
    bool c0_table_materialized_{};
};

} // namespace jojo