#include "core/ps1_boot_report_io.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static jojo::Ps1BootReport make_report(std::uint64_t retired) {
    jojo::Ps1BootReport report{};
    report.stop_reason = jojo::Ps1BootStopReason::bios_call_unimplemented;
    report.instructions_retired = retired;
    report.last_pc = 0x800000A0u;
    report.bios_call_count = 1u;
    report.recent_bios_calls.push_back({0x800000A0u, 0x000000A0u, 0x0000003Fu});
    report.recent_mmio.push_back({0x80010100u, 0x1F801070u, 4u, false, 0u});
    report.interrupts_accepted = 1u;
    report.dma_transfer_count = 2u;
    report.gpu_gp0_command_count = 3u;
    report.gpu_gp1_command_count = 4u;
    report.vram_write_count = 5u;
    report.presented_frames = 0u;
    return report;
}

int main() {
    const auto report = make_report(2u);
    const auto text = jojo::format_ps1_boot_report(report);
    CHECK(text.find("format=jojo-m3a-checkpoint-v1\n") == 0u);
    CHECK(text.find("stop_reason=bios_call_unimplemented\n") != std::string::npos);
    CHECK(text.find("instructions_retired=2\n") != std::string::npos);
    CHECK(text.find("last_pc=0x800000a0\n") != std::string::npos);
    CHECK(text.find("bios_last_selector=0x0000003f\n") != std::string::npos);
    CHECK(text.find("mmio_last_address=0x1f801070\n") != std::string::npos);
    CHECK(text.find("interrupts_accepted=1\n") != std::string::npos);
    CHECK(text.find("dma_transfer_count=2\n") != std::string::npos);
    CHECK(text.find("gpu_gp0_command_count=3\n") != std::string::npos);
    CHECK(text.find("gpu_gp1_command_count=4\n") != std::string::npos);
    CHECK(text.find("vram_write_count=5\n") != std::string::npos);
    CHECK(text.find("presented_frames=0\n") != std::string::npos);
    CHECK(text.find("PS-X EXE") == std::string::npos);

    const auto root = fs::temp_directory_path() / "jojo-m3a-report-io";
    const auto path = root / "diagnostics" / "m3a-checkpoint.txt";
    std::error_code ec;
    fs::remove_all(root, ec);

    auto first = jojo::save_ps1_boot_report_atomic(path, make_report(2u));
    CHECK(first);
    CHECK(fs::is_regular_file(path));
    CHECK(read_text(path).find("instructions_retired=2\n") != std::string::npos);

    auto second = jojo::save_ps1_boot_report_atomic(path, make_report(9u));
    CHECK(second);
    const auto replaced = read_text(path);
    CHECK(replaced.find("instructions_retired=9\n") != std::string::npos);
    CHECK(replaced.find("instructions_retired=2\n") == std::string::npos);

    fs::remove_all(root, ec);
    return failures ? 1 : 0;
}
