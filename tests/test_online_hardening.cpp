#include "core/online.h"

#include <iostream>

int main() {
    jojo::ModSetHashes hashes{"full-mod-set-hash", "gameplay-hash"};
    jojo::OnlineModPolicy exact{};
    exact.kind = jojo::OnlineModPolicyKind::exact_mod_set;

    const auto invalid_mode = static_cast<jojo::OnlineMode>(255);
    const auto policy = jojo::make_mod_session_policy(invalid_mode, exact, hashes);
    if (policy) {
        std::cerr << "invalid OnlineMode was accepted by make_mod_session_policy\n";
        return 1;
    }

    return 0;
}
