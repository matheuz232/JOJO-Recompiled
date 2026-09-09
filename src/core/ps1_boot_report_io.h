#pragma once

#include "core/ps1_boot_report.h"
#include "core/result.h"

#include <filesystem>
#include <string>

namespace jojo {

[[nodiscard]] std::string ps1_boot_stop_reason_name(Ps1BootStopReason reason) noexcept;
[[nodiscard]] std::string format_ps1_boot_report(const Ps1BootReport& report);
[[nodiscard]] Result<void> save_ps1_boot_report_atomic(
    const std::filesystem::path& path,
    const Ps1BootReport& report);

} // namespace jojo
