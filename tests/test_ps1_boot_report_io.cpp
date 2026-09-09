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
    report.recent_bios_calls.push_back({
        0x800000A0u, 0x000000A0u, 0x0000003Fu,
        0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0x80012345u});
    report.recent_mmio.push_back({0x80010100u, 0x1F801070u, 4u, false, 0u, false});
    report.interrupts_accepted = 1u;
    report.dma_transfer_count = 2u;
    report.gpu_gp0_command_count = 3u;
    report.gpu_gp1_command_count = 4u;
    report.vram_write_count = 5u;
    report.presented_frames = 0u;
    report.recent_trace.push_back({0x80010018u, 0x24420004u});
    report.recent_trace.push_back({0x8001001Cu, 0xAC400000u});
    return report;
}

int main() {
    const auto report = make_report(2u);
    const auto text = jojo::format_ps1_boot_report(report);
    CHECK(text.find("format=jojo-m3a-checkpoint-v1\n") == 0u);
    CHECK(text.find("stop_reason=bios_call_unimplemented\n") != std::string::npos);
    CHECK(text.find("instructions_retired=2\n") != std::string::npos);
    CHECK(text.find("last_pc=0x800000a0\n") != std::string::npos);
    CHECK(text.find("bios_event_count=1\n") != std::string::npos);
    CHECK(text.find("bios_event_0_selector=0x0000003f\n") != std::string::npos);
    CHECK(text.find("bios_event_0_a0=0x11111111\n") != std::string::npos);
    CHECK(text.find("bios_event_0_a1=0x22222222\n") != std::string::npos);
    CHECK(text.find("bios_event_0_a2=0x33333333\n") != std::string::npos);
    CHECK(text.find("bios_event_0_a3=0x44444444\n") != std::string::npos);
    CHECK(text.find("bios_event_0_ra=0x80012345\n") != std::string::npos);
    CHECK(text.find("bios_last_selector=0x0000003f\n") != std::string::npos);
    CHECK(text.find("bios_last_a0=0x11111111\n") != std::string::npos);
    CHECK(text.find("bios_last_ra=0x80012345\n") != std::string::npos);
    CHECK(text.find("mmio_last_address=0x1f801070\n") != std::string::npos);
    CHECK(text.find("interrupts_accepted=1\n") != std::string::npos);
    CHECK(text.find("dma_transfer_count=2\n") != std::string::npos);
    CHECK(text.find("gpu_gp0_command_count=3\n") != std::string::npos);
    CHECK(text.find("gpu_gp1_command_count=4\n") != std::string::npos);
    CHECK(text.find("vram_write_count=5\n") != std::string::npos);
    CHECK(text.find("presented_frames=0\n") != std::string::npos);
    CHECK(text.find("trace_sample_count=2\n") != std::string::npos);
    CHECK(text.find("trace_0_pc=0x80010018\n") != std::string::npos);
    CHECK(text.find("trace_0_opcode=0x24420004\n") != std::string::npos);
    CHECK(text.find("trace_1_pc=0x8001001c\n") != std::string::npos);
    CHECK(text.find("trace_1_opcode=0xac400000\n") != std::string::npos);
    CHECK(text.find("PS-X EXE") == std::string::npos);

    auto mega = make_report(77u);
    mega.diagnostic_probe_mode = true;
    mega.speculative_mmio_count = 2u;
    mega.recent_mmio.clear();
    mega.recent_mmio.push_back({0x80020000u, 0x1F801080u, 4u, true, 0x12345678u, true});
    mega.recent_mmio.push_back({0x80020004u, 0x1F801080u, 4u, false, 0x12345678u, true});
    const auto mega_text = jojo::format_ps1_boot_report(mega);
    CHECK(mega_text.find("format=jojo-mega-checkpoint-v1\n") == 0u);
    CHECK(mega_text.find("diagnostic_probe_mode=1\n") != std::string::npos);
    CHECK(mega_text.find("speculative_mmio_count=2\n") != std::string::npos);
    CHECK(mega_text.find("mmio_event_count=2\n") != std::string::npos);
    CHECK(mega_text.find("mmio_event_0_address=0x1f801080\n") != std::string::npos);
    CHECK(mega_text.find("mmio_event_0_speculative=1\n") != std::string::npos);
    CHECK(mega_text.find("mmio_event_1_value=0x12345678\n") != std::string::npos);

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
