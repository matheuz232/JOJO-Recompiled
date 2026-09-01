#pragma once
#include "core/conversion.h"
#include "core/psx_runtime.h"
#include "core/result.h"
#include <cstdint>
#include <filesystem>

namespace jojo {

struct InstallationInfo {
    std::filesystem::path install_dir;
    ConversionManifest manifest;
};

struct PsxRuntimeSliceResult {
    std::uint32_t executed_steps{};
    bool reached_budget{};
    PsxR3000aStepResult last_step{};
};

[[nodiscard]] Result<InstallationInfo> validate_installation(const std::filesystem::path& install_dir);
[[nodiscard]] Result<PsxRuntime> load_prepared_psx_runtime(const std::filesystem::path& install_dir);
[[nodiscard]] PsxRuntimeSliceResult run_psx_runtime_slice(
    PsxRuntime& runtime,
    const ResolvedInputFrame& input,
    std::uint32_t max_steps) noexcept;
[[nodiscard]] Result<void> bootstrap_runtime(const std::filesystem::path& install_dir);

}
