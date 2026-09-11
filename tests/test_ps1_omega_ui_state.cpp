#include "core/ps1_omega_ui_state.h"

#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1OmegaUiStateMachine state;
    CHECK(state.state() == jojo::Ps1OmegaUiState::idle);

    CHECK(state.transition(jojo::Ps1OmegaUiState::resumable));
    CHECK(state.state() == jojo::Ps1OmegaUiState::resumable);
    CHECK(state.transition(jojo::Ps1OmegaUiState::idle));

    CHECK(state.transition(jojo::Ps1OmegaUiState::running));
    CHECK(!state.transition(jojo::Ps1OmegaUiState::idle));
    CHECK(state.state() == jojo::Ps1OmegaUiState::running);
    CHECK(state.transition(jojo::Ps1OmegaUiState::stop_requested));
    CHECK(!state.transition(jojo::Ps1OmegaUiState::running));
    CHECK(state.transition(jojo::Ps1OmegaUiState::resumable));
    CHECK(state.transition(jojo::Ps1OmegaUiState::running));
    CHECK(state.transition(jojo::Ps1OmegaUiState::completed));

    CHECK(!state.transition(jojo::Ps1OmegaUiState::stop_requested));
    CHECK(state.transition(jojo::Ps1OmegaUiState::running));
    CHECK(state.transition(jojo::Ps1OmegaUiState::failed));
    CHECK(state.transition(jojo::Ps1OmegaUiState::running));
    CHECK(state.transition(jojo::Ps1OmegaUiState::resumable));

    return failures ? 1 : 0;
}
