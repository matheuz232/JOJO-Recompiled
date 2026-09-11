#include "core/ps1_omega_session_io.h"
#include "core/sha256.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)
}

int main() {
    using namespace jojo;

    const std::vector<std::uint8_t> empty;
    CHECK(sha256_hex(sha256(empty)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    CHECK(sha256_hex(sha256(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    Sha256Hasher incremental;
    const std::vector<std::uint8_t> a{'a'};
    const std::vector<std::uint8_t> bc{'b', 'c'};
    CHECK(incremental.update(a));
    CHECK(incremental.update(bc));
    const auto incremental_digest = incremental.finalize();
    CHECK(incremental_digest);
    if (incremental_digest) CHECK(incremental_digest.value == sha256(abc));
    CHECK(!incremental.update(a));
    CHECK(!incremental.finalize());

    const std::string long_text = "The quick brown fox jumps over the lazy dog";
    const std::vector<std::uint8_t> quick(long_text.begin(), long_text.end());
    CHECK(sha256_hex(sha256(quick)) == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    const auto root = std::filesystem::temp_directory_path() / "jojo_sha256_tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);
    const auto file = root / "payload.bin";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(long_text.data(), static_cast<std::streamsize>(long_text.size()));
    }
    const auto file_hash = sha256_file(file);
    CHECK(file_hash);
    if (file_hash) CHECK(file_hash.value == sha256(quick));

    const auto missing = sha256_file(root / "missing.bin");
    CHECK(!missing);

    const auto session_root = root / "omega-session";
    Ps1OmegaEvidenceRecorder recorder(session_root, 1024u * 1024u);
    CHECK(recorder.ready());
    const std::vector<std::uint8_t> mmio_payload{0x07u, 0x02u, 0x18u, 0x1Fu};
    const auto appended = recorder.append(Ps1OmegaEvidenceCategory::mmio, 1u, mmio_payload);
    CHECK(appended);
    if (appended) {
        CHECK(appended.record.sequence == 0u);
        CHECK(appended.record.epoch == 1u);
        CHECK(appended.record.category == Ps1OmegaEvidenceCategory::mmio);
        CHECK(appended.record.payload_bytes == mmio_payload.size());
        CHECK(std::filesystem::is_regular_file(session_root / appended.record.relative_path));
        CHECK(!std::filesystem::exists((session_root / appended.record.relative_path).string() + ".tmp"));
        CHECK(recorder.verify_chunk(appended.record));
    }
    CHECK(recorder.manifest().chunks.size() == 1u);
    CHECK(recorder.manifest().next_sequence == 1u);
    CHECK(recorder.manifest().committed_bytes >= mmio_payload.size());

    const auto orphan = session_root / "events" / "orphan.bin.tmp";
    std::filesystem::create_directories(orphan.parent_path(), ec);
    {
        std::ofstream out(orphan, std::ios::binary);
        out << "partial";
    }
    const auto reloaded = load_ps1_omega_session_manifest(session_root);
    CHECK(reloaded);
    if (reloaded) {
        CHECK(reloaded.value.chunks.size() == 1u);
        CHECK(reloaded.value.chunks.front().sequence == 0u);
    }

    const auto tiny_root = root / "omega-session-tiny";
    Ps1OmegaEvidenceRecorder tiny(tiny_root, 2u);
    CHECK(tiny.ready());
    const auto rejected = tiny.append(Ps1OmegaEvidenceCategory::mmio, 1u, mmio_payload);
    CHECK(!rejected);
    CHECK(rejected.status == Ps1OmegaSessionIoStatus::disk_budget_exhausted);
    CHECK(tiny.manifest().chunks.empty());
    CHECK(!std::filesystem::exists(tiny_root / "events" / "mmio-0000000000000000.bin"));

    std::filesystem::remove_all(root, ec);

    if (failures != 0) {
        std::cerr << failures << " sha256/session-io test(s) failed\n";
        return 1;
    }
    std::cout << "sha256/session-io tests passed\n";
    return 0;
}
