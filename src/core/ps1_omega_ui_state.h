#pragma once

#include <cstdint>

namespace jojo {

enum class Ps1OmegaUiState : std::uint8_t {
    idle,
    running,
    stop_requested,
    completed,
    failed,
    resumable,
};

class Ps1OmegaUiStateMachine {
public:
    [[nodiscard]] Ps1OmegaUiState state() const noexcept { return state_; }

    [[nodiscard]] bool transition(Ps1OmegaUiState next) noexcept {
        if (next == state_) return false;

        const bool allowed =
            (state_ == Ps1OmegaUiState::idle &&
                (next == Ps1OmegaUiState::running || next == Ps1OmegaUiState::resumable)) ||
            (state_ == Ps1OmegaUiState::resumable &&
                (next == Ps1OmegaUiState::idle || next == Ps1OmegaUiState::running)) ||
            (state_ == Ps1OmegaUiState::running &&
                (next == Ps1OmegaUiState::stop_requested ||
                 next == Ps1OmegaUiState::completed ||
                 next == Ps1OmegaUiState::failed ||
                 next == Ps1OmegaUiState::resumable)) ||
            (state_ == Ps1OmegaUiState::stop_requested &&
                (next == Ps1OmegaUiState::completed ||
                 next == Ps1OmegaUiState::failed ||
                 next == Ps1OmegaUiState::resumable)) ||
            (state_ == Ps1OmegaUiState::completed &&
                (next == Ps1OmegaUiState::running || next == Ps1OmegaUiState::resumable)) ||
            (state_ == Ps1OmegaUiState::failed &&
                (next == Ps1OmegaUiState::running || next == Ps1OmegaUiState::resumable));

        if (!allowed) return false;
        state_ = next;
        return true;
    }

private:
    Ps1OmegaUiState state_{Ps1OmegaUiState::idle};
};

} // namespace jojo
