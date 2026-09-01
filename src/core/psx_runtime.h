#pragma once
#include "core/psx_bus.h"
#include "core/psx_exe.h"
#include "core/psx_gte.h"
#include "core/psx_r3000a.h"
#include "core/psx_system_cnf.h"
#include "core/result.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace jojo {

struct PsxBiosEvent {
    bool allocated{};
    std::uint32_t event_class{};
    std::uint32_t status{};
    std::uint32_t spec{};
    std::uint32_t mode{};
    std::uint32_t callback{};
};

struct PsxBiosThread {
    bool allocated{};
    PsxR3000aState cpu{};
};

struct PsxBiosState {
    bool heap_initialized{};
    std::uint32_t heap_base{};
    std::uint32_t heap_size{};
    bool entry_interrupt_hook_installed{};
    std::uint32_t entry_interrupt_hook_address{};
    bool interrupt_return_state_valid{};
    PsxR3000aState interrupt_return_state{};
    bool pad_card_irq_completes{true};
    std::array<bool, 4> timer_vblank_irq_auto_ack{true, true, true, true};
    bool cdrom_irq_handlers_installed{true};
    bool c0_table_materialized{};
    bool b0_table_materialized{};
    bool card_initialized{};
    bool card_started{};
    bool card_pad_enabled{};
    bool early_card_irq_installed{};
    static constexpr std::size_t max_events = 32u;
    std::array<PsxBiosEvent, max_events> events{};
    std::uint32_t event_capacity{0x10u};
    bool event_table_initialized{};
    static constexpr std::size_t max_threads = 16u;
    std::array<PsxBiosThread, max_threads> threads{PsxBiosThread{true}};
    std::uint32_t thread_capacity{4u};
    std::uint32_t current_thread{};
};

struct PsxRuntime {
    PsxBus bus{};
    PsxR3000aState cpu{};
    PsxGteState gte{};
    PsxBiosState bios{};
};

[[nodiscard]] inline bool materialize_scph1001_exception_control_blocks(
    PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t table_of_tables_excb_address = 0x00000100u;
    constexpr std::uint32_t excb_address = 0xa000e004u;
    constexpr std::uint32_t excb_size = 4u * 8u;

    if (psx_bus_write_u32(runtime.bus, table_of_tables_excb_address,
                          excb_address) != PsxBusAccessReason::ok ||
        psx_bus_write_u32(runtime.bus, table_of_tables_excb_address + 4u,
                          excb_size) != PsxBusAccessReason::ok) {
        return false;
    }

    for (std::uint32_t priority = 0; priority < 4u; ++priority) {
        const auto entry = excb_address + priority * 8u;
        if (psx_bus_write_u32(runtime.bus, entry, 0u) != PsxBusAccessReason::ok ||
            psx_bus_write_u32(runtime.bus, entry + 4u, 0u) != PsxBusAccessReason::ok) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline Result<void> load_psx_boot_executable(
    PsxRuntime& runtime,
    std::span<const std::uint8_t> file,
    const PsxSystemCnf& system) {
    const auto parsed = parse_psx_exe(file);
    if (!parsed) return Result<void>::failure(parsed.error, parsed.detail);

    constexpr std::size_t header_size = 0x800u;
    const auto& exe = parsed.value;

    std::uint32_t physical_start = 0;
    if (!psx_bus_virtual_to_physical(exe.load_address, physical_start) ||
        physical_start >= PsxBus::default_ram_mirror_window) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS-X EXE load address is outside supported PS1 main RAM");
    }

    const auto physical_offset = physical_start & static_cast<std::uint32_t>(PsxBus::main_ram_size - 1u);
    const auto payload_size = static_cast<std::size_t>(exe.payload_size);
    if (static_cast<std::size_t>(physical_offset) + payload_size > PsxBus::main_ram_size) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "PS-X EXE payload would wrap across mirrored main RAM");
    }

    const auto requested_events = system.event == 0u ? 0x10u : system.event;
    if (requested_events > PsxBiosState::max_events) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "SYSTEM.CNF requests more PS1 events than this runtime supports");
    }
    const auto requested_threads = system.tcb == 0u ? 4u : system.tcb;
    if (requested_threads > PsxBiosState::max_threads) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "SYSTEM.CNF requests more PS1 threads than this runtime supports");
    }

    std::fill(runtime.bus.ram.begin(), runtime.bus.ram.end(), std::uint8_t{0});
    for (std::size_t i = 0; i < payload_size; ++i) {
        runtime.bus.ram[static_cast<std::size_t>(physical_offset) + i] = file[header_size + i];
    }

    if (!materialize_scph1001_exception_control_blocks(runtime)) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "Could not materialize SCPH-1001 exception control blocks");
    }

    runtime.bios.events.fill(PsxBiosEvent{});
    runtime.bios.event_capacity = requested_events;
    runtime.bios.event_table_initialized = false;
    runtime.bios.threads.fill(PsxBiosThread{});
    runtime.bios.threads[0].allocated = true;
    runtime.bios.thread_capacity = requested_threads;
    runtime.bios.current_thread = 0u;

    reset_psx_r3000a(runtime.cpu, exe.initial_pc);
    runtime.cpu.gpr[28] = exe.initial_gp;

    std::uint32_t stack = system.stack;
    if (stack == 0u && exe.stack_base != 0u) {
        const auto sum = static_cast<std::uint64_t>(exe.stack_base) +
                         static_cast<std::uint64_t>(exe.stack_offset);
        if (sum > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "PS-X EXE stack base plus offset overflows 32-bit address space");
        }
        stack = static_cast<std::uint32_t>(sum);
    }
    runtime.cpu.gpr[29] = stack;
    runtime.cpu.gpr[30] = stack;
    runtime.cpu.gpr[0] = 0u;
    runtime.bios.threads[0].cpu = runtime.cpu;

    return Result<void>::success();
}

[[nodiscard]] inline bool is_psx_bios_vector(std::uint32_t pc) noexcept {
    return pc == 0x000000a0u || pc == 0x000000b0u || pc == 0x000000c0u;
}

inline void return_from_psx_bios_call(PsxRuntime& runtime) noexcept {
    const auto return_pc = runtime.cpu.gpr[31];
    runtime.cpu.pc = return_pc;
    runtime.cpu.next_pc = return_pc + 4u;
    runtime.cpu.gpr[0] = 0u;
}

[[nodiscard]] inline bool restore_scph1001_default_entry_interrupt(PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t default_entry_address = 0x00006cf4u;
    constexpr std::array<std::uint32_t, 12> default_exit_structure{
        0x00000f40u, 0x000085d4u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u,
    };

    for (std::size_t i = 0; i < default_exit_structure.size(); ++i) {
        const auto address = default_entry_address + static_cast<std::uint32_t>(i * 4u);
        if (psx_bus_write_u32(runtime.bus, address, default_exit_structure[i]) !=
            PsxBusAccessReason::ok) {
            return false;
        }
    }

    runtime.bios.entry_interrupt_hook_installed = false;
    runtime.bios.entry_interrupt_hook_address = default_entry_address;
    runtime.cpu.gpr[2] = default_entry_address;
    return true;
}

[[nodiscard]] inline bool materialize_scph1001_c0_patch_surface(PsxRuntime& runtime) noexcept {
    if (runtime.bios.c0_table_materialized) return true;

    constexpr std::uint32_t c0_table_address = 0x00000674u;
    constexpr std::uint32_t exception_handler_address = 0x00000c80u;
    constexpr std::uint32_t exception_patch_address = exception_handler_address + 0x28u;
    constexpr std::array<std::uint32_t, 6> exception_patch_surface{
        0xaf410004u, 0xaf420008u, 0xaf43000cu,
        0xaf5f007cu, 0x40037000u, 0x00000000u,
    };

    if (psx_bus_write_u32(runtime.bus, c0_table_address + 6u * 4u,
                          exception_handler_address) != PsxBusAccessReason::ok) {
        return false;
    }
    for (std::size_t i = 0; i < exception_patch_surface.size(); ++i) {
        const auto address = exception_patch_address + static_cast<std::uint32_t>(i * 4u);
        if (psx_bus_write_u32(runtime.bus, address, exception_patch_surface[i]) !=
            PsxBusAccessReason::ok) {
            return false;
        }
    }

    runtime.bios.c0_table_materialized = true;
    return true;
}

[[nodiscard]] inline bool materialize_scph1001_b0_card_patch_surface(PsxRuntime& runtime) noexcept {
    if (runtime.bios.b0_table_materialized) return true;

    constexpr std::uint32_t b0_table_address = 0x00000874u;
    constexpr std::uint32_t change_clear_pad_address = 0x000043d0u;
    constexpr std::uint32_t card_delay_patch_address = change_clear_pad_address + 0x09c8u;
    constexpr std::array<std::uint32_t, 5> card_delay_patch_surface{
        0x946f000au, 0x3c080000u, 0x01e2c025u, 0x37190012u, 0xa479000au,
    };

    if (psx_bus_write_u32(runtime.bus, b0_table_address + 0x5bu * 4u,
                          change_clear_pad_address) != PsxBusAccessReason::ok) {
        return false;
    }
    for (std::size_t i = 0; i < card_delay_patch_surface.size(); ++i) {
        const auto address = card_delay_patch_address + static_cast<std::uint32_t>(i * 4u);
        if (psx_bus_write_u32(runtime.bus, address, card_delay_patch_surface[i]) !=
            PsxBusAccessReason::ok) {
            return false;
        }
    }

    runtime.bios.b0_table_materialized = true;
    return true;
}

inline void initialize_scph1001_event_slots(PsxRuntime& runtime) noexcept {
    if (runtime.bios.event_table_initialized) return;

    constexpr std::array<std::uint32_t, 5> cdrom_specs{
        0x10u, 0x20u, 0x40u, 0x80u, 0x8000u,
    };
    const auto reserved_count = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(cdrom_specs.size()), runtime.bios.event_capacity);
    for (std::uint32_t i = 0; i < reserved_count; ++i) {
        auto& event = runtime.bios.events[i];
        event = PsxBiosEvent{};
        event.allocated = true;
        event.event_class = 0xf0000003u;
        event.spec = cdrom_specs[i];
    }
    runtime.bios.event_table_initialized = true;
}

[[nodiscard]] inline std::uint32_t open_psx_bios_event(PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t disabled = 0x1000u;
    constexpr std::uint32_t handle_base = 0xf1000000u;

    initialize_scph1001_event_slots(runtime);
    for (std::uint32_t i = 0; i < runtime.bios.event_capacity; ++i) {
        auto& event = runtime.bios.events[i];
        if (event.allocated) continue;

        event.allocated = true;
        event.event_class = runtime.cpu.gpr[4];
        event.status = disabled;
        event.spec = runtime.cpu.gpr[5];
        event.mode = runtime.cpu.gpr[6];
        event.callback = runtime.cpu.gpr[7];
        return handle_base | i;
    }
    return 0xffffffffu;
}

inline void enable_psx_bios_event(PsxRuntime& runtime, std::uint32_t handle) noexcept {
    constexpr std::uint32_t handle_mask = 0xffff0000u;
    constexpr std::uint32_t handle_base = 0xf1000000u;
    constexpr std::uint32_t enabled_busy = 0x2000u;

    initialize_scph1001_event_slots(runtime);
    if ((handle & handle_mask) != handle_base) return;
    const auto index = handle & 0xffffu;
    if (index >= runtime.bios.event_capacity) return;
    auto& event = runtime.bios.events[index];
    if (!event.allocated) return;
    event.status = enabled_busy;
}

[[nodiscard]] inline std::uint32_t test_psx_bios_event(
    PsxRuntime& runtime, std::uint32_t handle) noexcept {
    constexpr std::uint32_t handle_mask = 0xffff0000u;
    constexpr std::uint32_t handle_base = 0xf1000000u;
    constexpr std::uint32_t enabled_busy = 0x2000u;
    constexpr std::uint32_t enabled_ready = 0x4000u;

    initialize_scph1001_event_slots(runtime);
    if ((handle & handle_mask) != handle_base) return 0u;
    const auto index = handle & 0xffffu;
    if (index >= runtime.bios.event_capacity) return 0u;
    auto& event = runtime.bios.events[index];
    if (!event.allocated) return 0u;
    if (event.status != enabled_ready) return 0u;

    event.status = enabled_busy;
    return 1u;
}

struct PsxBiosEventDelivery {
    std::uint32_t delivered{};
    std::uint32_t callback{};
};

[[nodiscard]] inline std::uint32_t open_psx_bios_thread(
    PsxRuntime& runtime, std::uint32_t pc, std::uint32_t sp,
    std::uint32_t gp) noexcept {
    constexpr std::uint32_t handle_base = 0xff000000u;
    for (std::uint32_t i = 0u; i < runtime.bios.thread_capacity; ++i) {
        auto& thread = runtime.bios.threads[i];
        if (thread.allocated) continue;
        thread = PsxBiosThread{};
        thread.allocated = true;
        reset_psx_r3000a(thread.cpu, pc);
        thread.cpu.gpr[28] = gp;
        thread.cpu.gpr[29] = sp;
        thread.cpu.gpr[30] = sp;
        return handle_base | i;
    }
    return 0xffffffffu;
}

[[nodiscard]] inline bool change_psx_bios_thread(
    PsxRuntime& runtime, std::uint32_t handle) noexcept {
    constexpr std::uint32_t handle_mask = 0xff000000u;
    constexpr std::uint32_t handle_base = 0xff000000u;
    if ((handle & handle_mask) != handle_base) return false;
    const auto index = handle & 0x00ffffffu;
    if (index >= runtime.bios.thread_capacity ||
        !runtime.bios.threads[index].allocated) {
        return false;
    }

    auto saved = runtime.cpu;
    saved.gpr[2] = 1u;
    saved.pc = saved.gpr[31];
    saved.next_pc = saved.pc + 4u;
    saved.pending_load_valid = false;
    saved.current_instruction_is_branch_delay_slot = false;
    saved.branch_pc = 0u;
    saved.gpr[0] = 0u;
    runtime.bios.threads[runtime.bios.current_thread].cpu = saved;

    runtime.cpu = runtime.bios.threads[index].cpu;
    runtime.cpu.gpr[0] = 0u;
    runtime.bios.current_thread = index;
    return true;
}

[[nodiscard]] inline PsxBiosEventDelivery deliver_psx_bios_event(
    PsxRuntime& runtime, std::uint32_t event_class, std::uint32_t spec) noexcept {
    constexpr std::uint32_t enabled_busy = 0x2000u;
    constexpr std::uint32_t enabled_ready = 0x4000u;
    constexpr std::uint32_t callback_mode = 0x1000u;
    constexpr std::uint32_t polling_mode = 0x2000u;

    initialize_scph1001_event_slots(runtime);
    PsxBiosEventDelivery result{};
    for (std::uint32_t i = 0u; i < runtime.bios.event_capacity; ++i) {
        auto& event = runtime.bios.events[i];
        if (!event.allocated || event.event_class != event_class ||
            event.spec != spec || event.status != enabled_busy) {
            continue;
        }
        if (event.mode == polling_mode) {
            event.status = enabled_ready;
            result.delivered = 1u;
        } else if (event.mode == callback_mode && event.callback != 0u &&
                   result.callback == 0u) {
            result.callback = event.callback;
            result.delivered = 1u;
        }
    }
    return result;
}

[[nodiscard]] inline PsxBusAccessReason dequeue_scph1001_interrupt_handler(
    PsxRuntime& runtime,
    std::uint32_t priority,
    std::uint32_t requested,
    std::uint32_t& removed) noexcept {
    removed = 0u;

    const auto excb_base = psx_bus_read_u32(runtime.bus, 0x00000100u);
    if (excb_base.reason != PsxBusAccessReason::ok) return excb_base.reason;

    std::uint32_t link_address = excb_base.value + priority * 8u;
    const auto first = psx_bus_read_u32(runtime.bus, link_address);
    if (first.reason != PsxBusAccessReason::ok) return first.reason;

    auto current = first.value;
    while (current != 0u) {
        if (current == requested) {
            const auto next = psx_bus_read_u32(runtime.bus, current);
            if (next.reason != PsxBusAccessReason::ok) return next.reason;
            const auto write = psx_bus_write_u32(runtime.bus, link_address, next.value);
            if (write != PsxBusAccessReason::ok) return write;
            removed = current;
            return PsxBusAccessReason::ok;
        }

        const auto next = psx_bus_read_u32(runtime.bus, current);
        if (next.reason != PsxBusAccessReason::ok) return next.reason;
        link_address = current;
        current = next.value;
    }

    return PsxBusAccessReason::ok;
}

[[nodiscard]] inline PsxBusAccessReason enqueue_scph1001_interrupt_handler(
    PsxRuntime& runtime,
    std::uint32_t priority,
    std::uint32_t node) noexcept {
    const auto excb_base = psx_bus_read_u32(runtime.bus, 0x00000100u);
    if (excb_base.reason != PsxBusAccessReason::ok) return excb_base.reason;

    const auto entry_address = excb_base.value + priority * 8u;
    const auto old_head = psx_bus_read_u32(runtime.bus, entry_address);
    if (old_head.reason != PsxBusAccessReason::ok) return old_head.reason;

    const auto head_write = psx_bus_write_u32(runtime.bus, entry_address, node);
    if (head_write != PsxBusAccessReason::ok) return head_write;
    return psx_bus_write_u32(runtime.bus, node, old_head.value);
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_gte_transfer(
    PsxRuntime& runtime, std::uint32_t instruction) noexcept {
    constexpr std::uint32_t cop2_enable = 1u << 30u;
    constexpr std::uint32_t cause_coprocessor_mask = 3u << 28u;
    constexpr std::uint32_t cause_coprocessor_2 = 2u << 28u;

    auto& cpu = runtime.cpu;
    const auto instruction_pc = cpu.pc;

    if ((cpu.cop0.status & cop2_enable) == 0u) {
        auto result = raise_psx_r3000a_exception(
            cpu, PsxR3000aExceptionCode::coprocessor_unusable,
            instruction_pc, instruction);
        cpu.cop0.cause = (cpu.cop0.cause & ~cause_coprocessor_mask) |
                         cause_coprocessor_2;
        return result;
    }

    const auto rs = static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
    const auto rt = static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
    const auto rd = static_cast<std::uint8_t>((instruction >> 11u) & 0x1fu);
    const bool canonical_move = (instruction & 0x7ffu) == 0u;
    const bool is_read = rs == 0x00u || rs == 0x02u;
    const bool is_write = rs == 0x04u || rs == 0x06u;

    if (!canonical_move || (!is_read && !is_write)) {
        return {PsxR3000aStepReason::unsupported_instruction,
                instruction_pc, instruction};
    }

    const auto sequential_pc = cpu.next_pc;
    const auto following_pc = sequential_pc + 4u;

    if (is_read) {
        const auto value = rs == 0x00u
            ? psx_gte_read_data(runtime.gte, rd)
            : psx_gte_read_control(runtime.gte, rd);

        const bool previous_load_valid = cpu.pending_load_valid;
        const auto previous_load_register = cpu.pending_load_register;
        const auto previous_load_value = cpu.pending_load_value;
        if (previous_load_valid &&
            previous_load_register != 0u &&
            previous_load_register != rt) {
            cpu.gpr[previous_load_register] = previous_load_value;
        }

        cpu.pending_load_valid = rt != 0u;
        cpu.pending_load_register = rt;
        cpu.pending_load_value = value;
    } else {
        if (rs == 0x04u) {
            psx_gte_write_data(runtime.gte, rd, cpu.gpr[rt]);
        } else {
            psx_gte_write_control(runtime.gte, rd, cpu.gpr[rt]);
        }
        complete_psx_pending_load(cpu);
    }

    cpu.gpr[0] = 0u;
    cpu.pc = sequential_pc;
    cpu.next_pc = following_pc;
    cpu.current_instruction_is_branch_delay_slot = false;
    cpu.branch_pc = 0u;
    return {PsxR3000aStepReason::ok, instruction_pc, instruction};
}

[[nodiscard]] inline bool handle_psx_syscall_exception(PsxRuntime& runtime) noexcept {
    constexpr std::uint32_t branch_delay_bit = 0x80000000u;
    constexpr std::uint32_t previous_interrupt_enable = 1u << 2u;
    constexpr std::uint32_t interrupt_mask_bit_2 = 1u << 10u;
    constexpr std::uint32_t critical_bits = previous_interrupt_enable | interrupt_mask_bit_2;

    if ((runtime.cpu.cop0.cause & branch_delay_bit) != 0u) return false;

    switch (runtime.cpu.gpr[4]) {
    case 0u:
        break;
    case 1u: {
        const bool both_were_enabled =
            (runtime.cpu.cop0.status & critical_bits) == critical_bits;
        runtime.cpu.cop0.status &= ~critical_bits;
        runtime.cpu.gpr[2] = both_were_enabled ? 1u : 0u;
        break;
    }
    case 2u:
        runtime.cpu.cop0.status |= critical_bits;
        break;
    default:
        return false;
    }

    runtime.cpu.cop0.status = (runtime.cpu.cop0.status & ~0x0fu) |
                              ((runtime.cpu.cop0.status >> 2u) & 0x0fu);
    const auto return_pc = runtime.cpu.cop0.epc + 4u;
    runtime.cpu.pc = return_pc;
    runtime.cpu.next_pc = return_pc + 4u;
    runtime.cpu.current_instruction_is_branch_delay_slot = false;
    runtime.cpu.branch_pc = 0u;
    runtime.cpu.gpr[0] = 0u;
    return true;
}

[[nodiscard]] inline bool handle_psx_interrupt_exception(
    PsxRuntime& runtime, const PsxR3000aState& interrupted_state) noexcept {
    if (!runtime.bios.entry_interrupt_hook_installed) return false;

    constexpr std::size_t saved_register_count = 12u;
    std::array<std::uint32_t, saved_register_count> saved{};
    for (std::size_t i = 0; i < saved.size(); ++i) {
        const auto word = psx_bus_read_u32(
            runtime.bus,
            runtime.bios.entry_interrupt_hook_address +
                static_cast<std::uint32_t>(i * sizeof(std::uint32_t)));
        if (word.reason != PsxBusAccessReason::ok) return false;
        saved[i] = word.value;
    }

    // SCPH-1001 HookEntryInt uses the same 30h layout as setjmp:
    // RA, SP, FP, S0-S7, GP. The completed exception handler longjmps to it
    // with v0=1 after running its IRQ queues.
    runtime.cpu.gpr[29] = saved[1];
    runtime.cpu.gpr[30] = saved[2];
    for (std::size_t i = 0; i < 8u; ++i) {
        runtime.cpu.gpr[16u + i] = saved[3u + i];
    }
    runtime.cpu.gpr[28] = saved[11];
    runtime.cpu.gpr[31] = saved[0];
    runtime.cpu.gpr[2] = 1u;
    runtime.cpu.gpr[0] = 0u;
    runtime.cpu.pending_load_valid = false;
    runtime.cpu.current_instruction_is_branch_delay_slot = false;
    runtime.cpu.branch_pc = 0u;
    runtime.cpu.pc = saved[0];
    runtime.cpu.next_pc = saved[0] + 4u;
    runtime.bios.interrupt_return_state = interrupted_state;
    runtime.bios.interrupt_return_state_valid = true;

    constexpr std::uint16_t vblank_interrupt = 1u << 0u;
    if (runtime.bios.timer_vblank_irq_auto_ack[0]) {
        runtime.bus.interrupt_status = static_cast<std::uint16_t>(
            runtime.bus.interrupt_status & ~vblank_interrupt);
    }
    return true;
}

[[nodiscard]] inline PsxR3000aStepResult step_psx_runtime(PsxRuntime& runtime) noexcept {
    const auto instruction_pc = runtime.cpu.pc;

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x44u) {
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x49u) {
        const auto write_reason = psx_bus_write_u32(
            runtime.bus, PsxBus::gpu_gp0_address, runtime.cpu.gpr[4]);
        if (write_reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x39u) {
        runtime.bios.heap_initialized = true;
        runtime.bios.heap_base = runtime.cpu.gpr[4];
        runtime.bios.heap_size = runtime.cpu.gpr[5];
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x70u) {
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000a0u && runtime.cpu.gpr[9] == 0x72u) {
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x08u) {
        runtime.cpu.gpr[2] = open_psx_bios_event(runtime);
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x07u) {
        const auto delivery = deliver_psx_bios_event(
            runtime, runtime.cpu.gpr[4], runtime.cpu.gpr[5]);
        runtime.cpu.gpr[2] = delivery.delivered;
        if (delivery.callback != 0u) {
            runtime.cpu.pc = delivery.callback;
            runtime.cpu.next_pc = delivery.callback + 4u;
            runtime.cpu.pending_load_valid = false;
            runtime.cpu.current_instruction_is_branch_delay_slot = false;
            runtime.cpu.branch_pc = 0u;
            runtime.cpu.gpr[0] = 0u;
        } else {
            return_from_psx_bios_call(runtime);
        }
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x0au) {
        const auto ready = test_psx_bios_event(runtime, runtime.cpu.gpr[4]);
        if (ready == 0u) {
            // A real WaitEvent sleeps until delivery. This single-threaded
            // runtime only completes the call when the event is already ready.
            return {PsxR3000aStepReason::unsupported_instruction,
                    instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = 1u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x0bu) {
        runtime.cpu.gpr[2] = test_psx_bios_event(runtime, runtime.cpu.gpr[4]);
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x0cu) {
        enable_psx_bios_event(runtime, runtime.cpu.gpr[4]);
        runtime.cpu.gpr[2] = 1u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x0eu) {
        runtime.cpu.gpr[2] = open_psx_bios_thread(
            runtime, runtime.cpu.gpr[4], runtime.cpu.gpr[5], runtime.cpu.gpr[6]);
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x10u) {
        if (!change_psx_bios_thread(runtime, runtime.cpu.gpr[4])) {
            return {PsxR3000aStepReason::unsupported_instruction,
                    instruction_pc, 0u};
        }
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x17u) {
        if (!runtime.bios.interrupt_return_state_valid) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }
        runtime.cpu = runtime.bios.interrupt_return_state;
        runtime.bios.interrupt_return_state_valid = false;
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x18u) {
        if (!restore_scph1001_default_entry_interrupt(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x19u) {
        runtime.bios.entry_interrupt_hook_installed = true;
        runtime.bios.entry_interrupt_hook_address = runtime.cpu.gpr[4];
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x35u) {
        const auto fd = runtime.cpu.gpr[4];
        const auto source = runtime.cpu.gpr[5];
        const auto length = runtime.cpu.gpr[6];
        if (fd != 1u) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }

        const auto end = static_cast<std::uint64_t>(source) +
                         static_cast<std::uint64_t>(length);
        if (end > (std::uint64_t{1} << 32u)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        for (std::uint32_t i = 0; i < length; ++i) {
            if (psx_bus_read_u8(runtime.bus, source + i).reason !=
                PsxBusAccessReason::ok) {
                return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
            }
        }

        runtime.cpu.gpr[2] = length;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x4au) {
        runtime.bios.card_initialized = true;
        runtime.bios.card_started = false;
        runtime.bios.card_pad_enabled = runtime.cpu.gpr[4] != 0u;
        runtime.bios.early_card_irq_installed = true;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x4bu) {
        runtime.bios.card_started = true;
        runtime.cpu.gpr[2] = 1u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x56u) {
        constexpr std::uint32_t c0_table_address = 0x00000674u;
        if (!materialize_scph1001_c0_patch_surface(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = c0_table_address;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x57u) {
        constexpr std::uint32_t b0_table_address = 0x00000874u;
        if (!materialize_scph1001_b0_card_patch_surface(runtime)) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }
        runtime.cpu.gpr[2] = b0_table_address;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000b0u && runtime.cpu.gpr[9] == 0x5bu) {
        runtime.bios.pad_card_irq_completes = runtime.cpu.gpr[4] != 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000c0u && runtime.cpu.gpr[9] == 0x02u) {
        const auto priority = runtime.cpu.gpr[4];
        const auto node = runtime.cpu.gpr[5];
        if (priority > 3u) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }

        const auto enqueue_reason = enqueue_scph1001_interrupt_handler(
            runtime, priority, node);
        if (enqueue_reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }

        runtime.cpu.gpr[2] = 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000c0u && runtime.cpu.gpr[9] == 0x03u) {
        const auto priority = runtime.cpu.gpr[4];
        const auto requested = runtime.cpu.gpr[5];
        if (priority > 3u) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }

        std::uint32_t removed = 0u;
        const auto dequeue_reason = dequeue_scph1001_interrupt_handler(
            runtime, priority, requested, removed);
        if (dequeue_reason != PsxBusAccessReason::ok) {
            return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
        }

        runtime.cpu.gpr[2] = removed;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (instruction_pc == 0x000000c0u && runtime.cpu.gpr[9] == 0x0au) {
        const auto timer = runtime.cpu.gpr[4];
        if (timer > 3u) {
            return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
        }
        const auto index = static_cast<std::size_t>(timer);
        const bool previous = runtime.bios.timer_vblank_irq_auto_ack[index];
        runtime.bios.timer_vblank_irq_auto_ack[index] = runtime.cpu.gpr[5] != 0u;
        runtime.cpu.gpr[2] = previous ? 1u : 0u;
        return_from_psx_bios_call(runtime);
        return {PsxR3000aStepReason::ok, instruction_pc, 0u};
    }

    if (is_psx_bios_vector(instruction_pc)) {
        return {PsxR3000aStepReason::unsupported_instruction, instruction_pc, 0u};
    }

    const auto fetched = psx_bus_read_u32(runtime.bus, instruction_pc);
    if (fetched.reason != PsxBusAccessReason::ok) {
        return {PsxR3000aStepReason::memory_fault, instruction_pc, 0u};
    }

    if ((fetched.value >> 26u) == 0x12u) {
        return step_psx_gte_transfer(runtime, fetched.value);
    }

    PsxR3000aState interrupted_state{};
    constexpr std::uint32_t current_interrupt_enable = 1u;
    constexpr std::uint32_t external_interrupt_mask = 1u << 10u;
    const bool external_interrupt_candidate =
        (runtime.bus.interrupt_status & runtime.bus.interrupt_mask &
         PsxBus::interrupt_status_valid_bits) != 0u &&
        (runtime.cpu.cop0.status & current_interrupt_enable) != 0u &&
        (runtime.cpu.cop0.status & external_interrupt_mask) != 0u;
    if (external_interrupt_candidate) interrupted_state = runtime.cpu;

    const auto stepped = step_psx_r3000a(runtime.cpu, fetched.value, runtime.bus);
    if (stepped.reason == PsxR3000aStepReason::ok ||
        stepped.reason == PsxR3000aStepReason::exception) {
        psx_bus_tick(runtime.bus, 1u);
    }
    if (stepped.reason == PsxR3000aStepReason::exception &&
        stepped.exception_code == PsxR3000aExceptionCode::syscall &&
        handle_psx_syscall_exception(runtime)) {
        return {PsxR3000aStepReason::ok, stepped.instruction_pc, stepped.instruction};
    }
    if (stepped.reason == PsxR3000aStepReason::exception &&
        stepped.exception_code == PsxR3000aExceptionCode::interrupt &&
        external_interrupt_candidate &&
        handle_psx_interrupt_exception(runtime, interrupted_state)) {
        return {PsxR3000aStepReason::ok, stepped.instruction_pc, stepped.instruction};
    }
    return stepped;
}

}
