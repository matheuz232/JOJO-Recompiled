#include "core/runtime.h"
#include "core/psx_system_cnf.h"
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace jojo {
namespace {

Result<std::vector<std::uint8_t>> read_binary_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_installation,
            "prepared runtime file is missing: " + path.string());
    }
    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (!in.eof() && in.fail()) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error,
            "failed while reading prepared runtime file: " + path.string());
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

}

Result<InstallationInfo> validate_installation(const std::filesystem::path& install_dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(install_dir, ec) || ec) {
        return Result<InstallationInfo>::failure(ErrorCode::invalid_installation,
                                                 "installation directory does not exist");
    }
    if (!std::filesystem::is_directory(install_dir / "data", ec) || ec) {
        return Result<InstallationInfo>::failure(ErrorCode::invalid_installation,
                                                 "installation is missing data directory");
    }
    if (!std::filesystem::is_directory(install_dir / "cache", ec) || ec) {
        return Result<InstallationInfo>::failure(ErrorCode::invalid_installation,
                                                 "installation is missing cache directory");
    }
    auto manifest = load_conversion_manifest(install_dir / "game_manifest.ini");
    if (!manifest) {
        return Result<InstallationInfo>::failure(manifest.error, manifest.detail);
    }
    return Result<InstallationInfo>::success(InstallationInfo{install_dir, std::move(manifest.value)});
}

Result<PsxRuntime> load_prepared_psx_runtime(const std::filesystem::path& install_dir) {
    auto install = validate_installation(install_dir);
    if (!install) return Result<PsxRuntime>::failure(install.error, install.detail);

    if (install.value.manifest.backend != "psx-runtime-prepared") {
        return Result<PsxRuntime>::failure(
            ErrorCode::backend_unavailable,
            "converted installation does not contain a prepared PS1 runtime");
    }

    auto system_bytes = read_binary_file(install_dir / "data" / "SYSTEM.CNF");
    if (!system_bytes) return Result<PsxRuntime>::failure(system_bytes.error, system_bytes.detail);
    const std::string system_text(system_bytes.value.begin(), system_bytes.value.end());
    auto system = parse_psx_system_cnf(system_text);
    if (!system) return Result<PsxRuntime>::failure(system.error, system.detail);

    auto executable = read_binary_file(install_dir / "data" / "PSX.EXE");
    if (!executable) return Result<PsxRuntime>::failure(executable.error, executable.detail);

    PsxRuntime runtime{};
    auto loaded = load_psx_boot_executable(runtime, executable.value, system.value);
    if (!loaded) return Result<PsxRuntime>::failure(loaded.error, loaded.detail);

    return Result<PsxRuntime>::success(std::move(runtime));
}

Result<void> bootstrap_runtime(const std::filesystem::path& install_dir) {
    auto install = validate_installation(install_dir);
    if (!install) return Result<void>::failure(install.error, install.detail);

    if (install.value.manifest.backend == "native-ready") {
        return Result<void>::success();
    }

    if (install.value.manifest.backend != "psx-runtime-prepared") {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "converted installation is valid, but no prepared PS1 runtime is available yet");
    }

    auto runtime = load_prepared_psx_runtime(install_dir);
    if (!runtime) return Result<void>::failure(runtime.error, runtime.detail);
    return Result<void>::success();
}

}
