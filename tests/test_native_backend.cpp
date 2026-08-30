#include "core/native_backend.h"
#include "core/version.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::DreamcastBootProgram synthetic_program(std::uint8_t add_imm = 2u) {
    jojo::DreamcastBootProgram program{};
    program.metadata.device_info = "GD-ROM1/1";
    program.bytes = {
        0x01u, 0xE0u,             // MOV #1,R0
        add_imm, 0x70u,           // ADD #imm,R0
        0x09u, 0x00u,             // NOP
    };
    return program;
}

static std::filesystem::path temp_install(const char* suffix) {
    auto root = std::filesystem::temp_directory_path() /
                (std::string("jojo-m3-native-") + suffix);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "cache", ec);
    return root;
}

static const jojo::NativeCompiledBlock* first_native_block(const jojo::NativeBackend& backend) {
    for (const auto& block : backend.blocks) {
        if (block.uses_native_lowering) return &block;
    }
    return nullptr;
}

static void test_compiles_backend_and_steps_deterministically() {
    auto left = jojo::create_native_runtime(synthetic_program());
    auto right = jojo::create_native_runtime(synthetic_program());
    CHECK(left && right);
    if (!left || !right) return;

    CHECK(left.value.backend.abi_version == jojo::native_backend_abi_version());
    CHECK(!left.value.backend.program_hash.empty());
    CHECK(left.value.backend.ir.blocks.size() == right.value.backend.ir.blocks.size());
    CHECK(left.value.backend.native_block_count > 0u);
    const auto* native = first_native_block(left.value.backend);
    CHECK(native != nullptr);
    if (native) {
        CHECK(native->native_code.size() > 1u);
        CHECK(native->native_code.back() == 0xC3u); // x86-64 RET
    }
    CHECK(left.value.frame_index == 0u);
    CHECK(left.value.cpu.pc == jojo::kDreamcastBootLoadAddress);

    const auto left_step = jojo::step_native_frame(left.value, 8u);
    const auto right_step = jojo::step_native_frame(right.value, 8u);
    CHECK(left_step && right_step);
    if (!left_step || !right_step) return;

    CHECK(!left_step.value.used_reference_fallback);
    CHECK(!right_step.value.used_reference_fallback);
    CHECK(left_step.value.native_code_executed);
    CHECK(right_step.value.native_code_executed);
    CHECK(left.value.cpu.r[0] == 3u);
    CHECK(right.value.cpu.r[0] == 3u);
    CHECK(left.value.frame_index == 1u);
    CHECK(right.value.frame_index == 1u);
    CHECK(left_step.value.blocks_executed == right_step.value.blocks_executed);
    CHECK(left_step.value.operations_executed == right_step.value.operations_executed);
    CHECK(left.value.state_hash == right.value.state_hash);
    CHECK(left.value.state_hash != 0u);
}

static void test_cache_is_written_reused_and_rebuilt_on_abi_or_program_change() {
    const auto install = temp_install("cache");
    const auto first = jojo::ensure_native_backend_cache(synthetic_program(), install);
    CHECK(first);
    if (!first) return;

    CHECK(first.value.rebuilt);
    CHECK(first.value.abi_version == jojo::native_backend_abi_version());
    CHECK(first.value.block_count > 0u);
    CHECK(first.value.operation_count >= 3u);
    CHECK(first.value.native_code_bytes > 0u);
    CHECK(first.value.manifest_path == install / "cache" / "native" / "backend_cache.ini");
    CHECK(first.value.plan_path == install / "cache" / "native" / "compiled_plan.bin");
    CHECK(std::filesystem::is_regular_file(first.value.manifest_path));
    CHECK(std::filesystem::is_regular_file(first.value.plan_path));

    const auto loaded = jojo::load_native_backend_cache(first.value.plan_path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.abi_version == jojo::native_backend_abi_version());
        CHECK(loaded.value.program_hash == first.value.program_hash);
        CHECK(loaded.value.ir.blocks.size() == first.value.block_count);
        CHECK(loaded.value.native_block_count > 0u);
        const auto* loaded_native = first_native_block(loaded.value);
        CHECK(loaded_native != nullptr);
        if (loaded_native) CHECK(!loaded_native->native_code.empty());
    }

    const auto second = jojo::ensure_native_backend_cache(synthetic_program(), install);
    CHECK(second);
    if (second) {
        CHECK(!second.value.rebuilt);
        CHECK(second.value.program_hash == first.value.program_hash);
        CHECK(second.value.native_code_bytes == first.value.native_code_bytes);
    }

    {
        std::ofstream out(first.value.manifest_path, std::ios::trunc);
        out << "abi_version=" << jojo::native_backend_abi_version() << "\n";
        out << "core_version=stale-core-version\n";
        out << "program_hash=" << first.value.program_hash << "\n";
        out << "block_count=" << first.value.block_count << "\n";
        out << "operation_count=" << first.value.operation_count << "\n";
        out << "native_code_bytes=" << first.value.native_code_bytes << "\n";
    }
    const auto core_rebuild = jojo::ensure_native_backend_cache(synthetic_program(), install);
    CHECK(core_rebuild);
    if (core_rebuild) CHECK(core_rebuild.value.rebuilt);

    {
        std::ofstream out(first.value.manifest_path, std::ios::trunc);
        out << "abi_version=0\n";
        out << "core_version=" << jojo::core_version() << "\n";
        out << "program_hash=" << first.value.program_hash << "\n";
        out << "block_count=" << first.value.block_count << "\n";
        out << "operation_count=" << first.value.operation_count << "\n";
        out << "native_code_bytes=" << first.value.native_code_bytes << "\n";
    }
    const auto abi_rebuild = jojo::ensure_native_backend_cache(synthetic_program(), install);
    CHECK(abi_rebuild);
    if (abi_rebuild) CHECK(abi_rebuild.value.rebuilt);

    const auto changed = jojo::ensure_native_backend_cache(synthetic_program(3u), install);
    CHECK(changed);
    if (changed) {
        CHECK(changed.value.rebuilt);
        CHECK(changed.value.program_hash != first.value.program_hash);
    }

    std::error_code ec;
    std::filesystem::remove_all(install, ec);
}

static void test_cache_manifest_records_current_backend_version() {
    const auto install = temp_install("version");
    const auto cache = jojo::ensure_native_backend_cache(synthetic_program(), install);
    CHECK(cache);
    if (!cache) return;

    std::ifstream in(cache.value.manifest_path);
    CHECK(static_cast<bool>(in));
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("abi_version=" + std::to_string(jojo::native_backend_abi_version())) != std::string::npos);
    CHECK(text.find("core_version=") != std::string::npos);
    CHECK(text.find("program_hash=" + cache.value.program_hash) != std::string::npos);
    CHECK(text.find("native_code_bytes=" + std::to_string(cache.value.native_code_bytes)) != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(install, ec);
}

int main() {
    test_compiles_backend_and_steps_deterministically();
    test_cache_is_written_reused_and_rebuilt_on_abi_or_program_change();
    test_cache_manifest_records_current_backend_version();
    if (failures) {
        std::cerr << failures << " M3 native-backend assertion(s) failed\n";
        return 1;
    }
    std::cout << "all M3 native-backend assertions passed\n";
    return 0;
}
