#pragma once

#include "core/r3000a_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace jojo {

class Ps1MemoryBus;
class Ps1HleBios;

enum class Ps1InterruptContinuationPhase : std::uint8_t {
    inactive,
    dispatch,
    first_callback,
    second_callback,
    hook_guest,
};

struct Ps1InterruptedContext {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t status{};
    std::uint32_t resume_pc{};
    std::uint32_t resume_next_pc{};
};

class Ps1InterruptContinuation {
public:
    static constexpr std::uint32_t callback_return_sentinel = 0xE00000F0u;

    void begin(const R3000aState& post_exception,
               std::uint32_t pre_exception_status,
               std::uint32_t resume_pc,
               std::uint32_t resume_next_pc) noexcept;
    void return_from_exception(R3000aState& cpu) noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] Ps1InterruptContinuationPhase phase() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    Ps1InterruptContinuationPhase phase_{Ps1InterruptContinuationPhase::inactive};
    Ps1InterruptedContext saved_{};
    std::uint8_t priority_{};
    std::uint32_t current_node_{};
    std::uint32_t next_node_{};
    std::uint32_t first_callback_{};
    std::uint32_t second_callback_{};
    bool priority_head_loaded_{};
    std::array<std::uint32_t, 64> visited_nodes_{};
    std::size_t visited_count_{};
};

} // namespace jojo
