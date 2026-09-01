#include "core/iso9660.h"
#include "core/psx_boot.h"
#include "core/psx_diagnostics.h"
#include "core/psx_revision.h"
#include "core/psx_runtime.h"
#include "core/revision.h"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t execution_limit = 400000000u;

const char* step_reason_name(jojo::PsxR3000aStepReason reason) noexcept {
    switch (reason) {
    case jojo::PsxR3000aStepReason::ok: return "ok";
    case jojo::PsxR3000aStepReason::unsupported_instruction: return "unsupported-instruction";
    case jojo::PsxR3000aStepReason::memory_fault: return "memory-fault";
    case jojo::PsxR3000aStepReason::exception: return "exception";
    }
    return "unknown";
}

bool is_bios_vector(std::uint32_t pc) noexcept {
    return pc == 0x000000a0u || pc == 0x000000b0u || pc == 0x000000c0u;
}

void print_bios_context(const jojo::PsxRuntime& runtime, std::uint32_t pc) {
    if (!is_bios_vector(pc)) return;

    std::cout << std::hex << std::showbase;
    std::cout << "bios_vector=" << pc << '\n';
    std::cout << "bios_function=" << runtime.cpu.gpr[9] << '\n';
    std::cout << "a0=" << runtime.cpu.gpr[4] << '\n';
    std::cout << "a1=" << runtime.cpu.gpr[5] << '\n';
    std::cout << "a2=" << runtime.cpu.gpr[6] << '\n';
    std::cout << "a3=" << runtime.cpu.gpr[7] << '\n';
    std::cout << "ra=" << runtime.cpu.gpr[31] << '\n';
    std::cout << std::dec << std::noshowbase;
}

void print_exception_context(const jojo::PsxRuntime& runtime,
                             const jojo::PsxR3000aStepResult& result) {
    if (result.reason != jojo::PsxR3000aStepReason::exception) return;

    std::cout << "exception_code="
              << jojo::psx_r3000a_exception_code_name(result.exception_code) << '\n';
    std::cout << "branch_delay_slot="
              << (jojo::psx_r3000a_exception_in_branch_delay_slot(runtime.cpu) ? 1 : 0)
              << '\n';
    std::cout << std::hex << std::showbase;
    std::cout << "cop0_cause=" << runtime.cpu.cop0.cause << '\n';
    std::cout << "cop0_epc=" << runtime.cpu.cop0.epc << '\n';
    std::cout << "cop0_badvaddr=" << runtime.cpu.cop0.bad_vaddr << '\n';
    std::cout << "cop0_status=" << runtime.cpu.cop0.status << '\n';
    std::cout << std::dec << std::noshowbase;
}

void print_instruction_context(const jojo::PsxRuntime& runtime, std::uint32_t pc) {
    print_bios_context(runtime, pc);

    const auto fetched = jojo::psx_bus_read_u32(runtime.bus, pc);
    if (fetched.reason != jojo::PsxBusAccessReason::ok) {
        std::cout << "instruction_class=unavailable\n";
        return;
    }

    const auto instruction = fetched.value;
    const auto primary = static_cast<std::uint8_t>(instruction >> 26u);
    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    const auto signed_immediate = static_cast<std::int32_t>(
        static_cast<std::int16_t>(instruction & 0xffffu));
    const auto effective_address = runtime.cpu.gpr[rs] +
        static_cast<std::uint32_t>(signed_immediate);

    std::cout << std::hex << std::showbase;
    std::cout << "instruction=" << instruction << '\n';
    std::cout << "primary_opcode=" << static_cast<unsigned>(primary) << '\n';
    if (primary == 0u) {
        std::cout << "special_funct=" << static_cast<unsigned>(instruction & 0x3fu) << '\n';
    }
    std::cout << "rs=" << static_cast<unsigned>(rs) << '\n';
    std::cout << "rt=" << static_cast<unsigned>(rt) << '\n';
    std::cout << "rs_value=" << runtime.cpu.gpr[rs] << '\n';
    std::cout << "rt_value=" << runtime.cpu.gpr[rt] << '\n';
    std::cout << "effective_address=" << effective_address << '\n';
    std::cout << std::dec << std::noshowbase;
    std::cout << "signed_immediate=" << signed_immediate << '\n';
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
            print_exception_context(runtime, result);
            print_instruction_context(runtime, instruction_pc);
            std::cout << "cdrom_index=" << static_cast<unsigned>(runtime.bus.cdrom_index) << '\n';
            std::cout << "cdrom_interrupt_flags="
                      << static_cast<unsigned>(runtime.bus.cdrom_interrupt_flags) << '\n';
            std::cout << "cdrom_result_count="
                      << static_cast<unsigned>(runtime.bus.cdrom_result_count) << '\n';
            std::cout << "cdrom_parameter_count="
                      << static_cast<unsigned>(runtime.bus.cdrom_parameter_count) << '\n';
            std::cout << "dma6_madr=0x" << std::hex << runtime.bus.dma6_base << '\n';
            std::cout << "dma6_bcr=0x" << runtime.bus.dma6_block_control << '\n';
            return 0;
        }
    }

    std::cout << "executed=" << execution_limit << '\n';
    std::cout << "reason=limit\n";
    std::cout << std::hex << std::showbase;
    std::cout << "timer1_mode=" << runtime.bus.timer1_mode << '\n';
    std::cout << "timer1_current=" << runtime.bus.timer1_current << '\n';
    const auto nonzero_vram_pixels = std::count_if(
        runtime.bus.gpu_vram.begin(), runtime.bus.gpu_vram.end(),
        [](std::uint16_t pixel) { return pixel != 0u; });
    std::cout << "nonzero_vram_pixels=" << nonzero_vram_pixels << '\n';
    std::cout << std::dec << std::noshowbase;
    std::cout << std::hex << std::showbase << "stop_pc=" << runtime.cpu.pc << '\n';
    std::cout << std::dec << std::noshowbase;
    print_instruction_context(runtime, runtime.cpu.pc);
    std::cout << "a0=0x" << std::hex << runtime.cpu.gpr[4] << '\n';
    const auto wait_counter = jojo::psx_bus_read_u32(runtime.bus, 0x80062780u);
    if (wait_counter.reason == jojo::PsxBusAccessReason::ok) {
        std::cout << "wait_counter_80062780=0x" << wait_counter.value << '\n';
    }
    std::cout << "vblank_callback_80062784=0x"
              << jojo::psx_bus_read_u32(runtime.bus, 0x80062784u).value << '\n';
    const auto excb = jojo::psx_bus_read_u32(runtime.bus, 0x100u);
    std::cout << "irq_hook=0x" << runtime.bios.entry_interrupt_hook_address << '\n';
    std::cout << "irq_hook_ra=0x"
              << jojo::psx_bus_read_u32(
                     runtime.bus, runtime.bios.entry_interrupt_hook_address).value
              << '\n';
    if (excb.reason == jojo::PsxBusAccessReason::ok) {
        for (std::uint32_t priority = 0u; priority < 4u; ++priority) {
            auto node = jojo::psx_bus_read_u32(
                runtime.bus, excb.value + priority * 8u).value;
            for (std::uint32_t index = 0u; node != 0u && index < 8u; ++index) {
                std::cout << "irq_node_p" << priority << '_' << index
                          << "=0x" << node << '\n';
                std::cout << "irq_node_first=0x"
                          << jojo::psx_bus_read_u32(runtime.bus, node + 8u).value << '\n';
                std::cout << "irq_node_second=0x"
                          << jojo::psx_bus_read_u32(runtime.bus, node + 4u).value << '\n';
                node = jojo::psx_bus_read_u32(runtime.bus, node).value;
            }
        }
    }
    for (std::int32_t delta = -24; delta <= 8; ++delta) {
        const auto address = runtime.cpu.pc +
            static_cast<std::uint32_t>(delta * 4);
        const auto word = jojo::psx_bus_read_u32(runtime.bus, address);
        if (word.reason == jojo::PsxBusAccessReason::ok) {
            std::cout << "code_0x" << std::hex << address << "=0x"
                      << word.value << '\n';
        }
    }
    for (std::uint32_t address = 0x8003c660u; address <= 0x8003c740u;
         address += 4u) {
        std::cout << "hook_code_0x" << std::hex << address << "=0x"
                  << jojo::psx_bus_read_u32(runtime.bus, address).value << '\n';
    }
    for (std::uint32_t address = 0x8003cc20u; address <= 0x8003cca0u;
         address += 4u) {
        std::cout << "irq_code_0x" << std::hex << address << "=0x"
                  << jojo::psx_bus_read_u32(runtime.bus, address).value << '\n';
    }
    for (std::uint32_t address = 0x8003c4d0u; address <= 0x8003c550u;
         address += 4u) {
        std::cout << "callback_code_0x" << std::hex << address << "=0x"
                  << jojo::psx_bus_read_u32(runtime.bus, address).value << '\n';
    }
    return 0;
}
