#include "core/psx_revision.h"

namespace jojo {

const std::vector<GameRevisionProfile>& supported_psx_game_revision_profiles() {
    static const std::vector<GameRevisionProfile> profiles = {
        {
            "ps1-usa-slus-01060",
            {
                {"/SYSTEM.CNF", 68u, 0x1eb36f6335bbf54aull},
                {"/SLUS_010.60", 565248u, 0xb84be235e572adccull},
            },
        },
    };
    return profiles;
}

}
