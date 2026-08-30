#include "core/iso9660.h"
#include "core/psx_boot.h"
#include "core/psx_revision.h"
#include "core/psx_runtime.h"
#include "core/revision.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t execution_limit = 10000000u;

const char* step_reason_name(jojo::PsxR3000aStepReason reason) noexcept {
    switch (reason) {
    case jojo::PsxR3000aStepReason::ok: return "ok";
    case jojo::PsxR3000aStepReason::unsupported_instruction: return "unsupported-instruction";
    case jojo::PsxR3000aStepReason::memory_fault: return "memory-fault";
    }
    return "unknown";
}

void print_instruction_class(const jojo::PsxRuntime& runtime, std::uint32_t pc) {
    const auto fetched = jojo::psx_bus_read_u32(runtime.bus, pc);
    if (fetched.reason != jojo::PsxBusAccessReason::ok) {
        std::cout << "instruction_class=unavailable\n";
        return;
    }

    const auto primary = static_cast<std::uint8_t>(fetched.value >> 26u);
    std::cout << std::hex << std::showbase;
    std::cout << "primary_opcode=" << static_cast<unsigned>(primary) << '\n';
    if (primary == 0u) {
        std::cout << "special_funct=" << static_cast<unsigned>(fetched.value & 0x3fu) << '\n';
    }
    std::cout << std::dec << std::noshowbase;
}

}

int main(int argc, char** argv) {
    bool execute = false;
    const char* media_path = nullptr;

    if (argc == 2) {
        media_path = argv[1];
    } else if (argc == 3 && std::string_view(argv[1]) == "--execute") {
        execute = true;
        media_path = argv[2];
    } else {
        std::cerr << "usage: jojo_r1_media_probe [--execute] <cue|bin|iso>\n";
        return 2;
    }

    const auto image = jojo::open_iso9660(media_path);
    if (!image) {
        std::cerr << "open_error=" << image.detail << '\n';
        return 3;
    }

    const auto boot = jojo::analyze_psx_boot(image.value);
    if (!boot) {
        std::cerr << "boot_error=" << boot.detail << '\n';
        return 4;
    }

    const auto revision = jojo::identify_game_revision(
        image.value, jojo::supported_psx_game_revision_profiles());
    if (!revision) {
        std::cerr << "revision_error=" << revision.detail << '\n';
        return 5;
    }

    std::cout << "revision=" << revision.value.revision_id << '\n';
    std::cout << "boot=" << boot.value.executable_path << '\n';
    std::cout << std::hex << std::showbase;
    std::cout << "pc=" << boot.value.executable.initial_pc << '\n';
    std::cout << "load=" << boot.value.executable.load_address << '\n';
    std::cout << "payload=" << boot.value.executable.payload_size << '\n';
    std::cout << "stack=" << boot.value.executable.stack_base << '\n';
    std::cout << std::dec << std::noshowbase;

    if (!execute) return 0;

    const auto executable = jojo::read_iso9660_file(image.value, boot.value.executable_path);
    if (!executable) {
        std::cerr << "executable_read_error=" << executable.detail << '\n';
        return 6;
    }

    jojo::PsxRuntime runtime{};
    const auto loaded = jojo::load_psx_boot_executable(runtime, executable.value, boot.value.system);
    if (!loaded) {
        std::cerr << "runtime_load_error=" << loaded.detail << '\n';
        return 7;
    }

    for (std::uint64_t executed = 0; executed < execution_limit; ++executed) {
        const auto instruction_pc = runtime.cpu.pc;
        const auto result = jojo::step_psx_runtime(runtime);
        if (result.reason != jojo::PsxR3000aStepReason::ok) {
            std::cout << "executed=" << executed << '\n';
            std::cout << "reason=" << step_reason_name(result.reason) << '\n';
            std::cout << std::hex << std::showbase << "stop_pc=" << instruction_pc << '\n';
            std::cout << std::dec << std::noshowbase;
            print_instruction_class(runtime, instruction_pc);
            return 0;
        }
    }

    std::cout << "executed=" << execution_limit << '\n';
    std::cout << "reason=limit\n";
    std::cout << std::hex << std::showbase << "stop_pc=" << runtime.cpu.pc << '\n';
    std::cout << std::dec << std::noshowbase;
    print_instruction_class(runtime, runtime.cpu.pc);
    return 0;
}
