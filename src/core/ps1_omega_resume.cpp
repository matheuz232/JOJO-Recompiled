#include "core/ps1_omega_infinity.h"

#include "core/ps1_omega_scheduler.h"
#include "core/ps1_omega_session_io.h"
#include "core/sha256.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace jojo {

bool ps1_omega_infinity_has_compatible_resumable_session(
    const std::filesystem::path& session_root,
    std::string_view executable_identity) {
    const auto manifest = load_ps1_omega_session_manifest(session_root);
    if (!manifest) return false;

    for (auto it = manifest.value.chunks.rbegin(); it != manifest.value.chunks.rend(); ++it) {
        if (it->category != Ps1OmegaEvidenceCategory::search) continue;

        const auto path = session_root / it->relative_path;
        const auto digest = sha256_file(path);
        if (!digest || sha256_hex(digest.value) != it->sha256) return false;

        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        if (bytes.size() != kPs1OmegaChunkHeaderBytes + it->payload_bytes) return false;

        const auto payload_begin = bytes.begin() +
            static_cast<std::ptrdiff_t>(kPs1OmegaChunkHeaderBytes);
        std::vector<std::uint8_t> payload(payload_begin, bytes.end());
        const auto resume = decode_ps1_omega_resume_state(payload);
        return resume.has_value() &&
               !resume->pending.empty() &&
               resume->executable_identity == executable_identity;
    }
    return false;
}

} // namespace jojo
