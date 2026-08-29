#include "core/dreamcast_boot_runner.h"

#include "core/dreamcast_bus.h"
#include "core/dreamcast_interrupts.h"
#include "core/dreamcast_system_asic.h"
#include "core/sh4_cfg.h"
#include "core/sh4_ir.h"

namespace jojo {

Result<DreamcastBootRunResult> run_dreamcast_boot_reference(
    const DreamcastBootProgram& program,
    Sh4ReferenceState initial_state,
    std::size_t max_blocks) {
    if (max_blocks == 0u) {
        return Result<DreamcastBootRunResult>::failure(
            ErrorCode::invalid_argument, "Dreamcast boot block limit must be non-zero");
    }

    auto loaded = load_dreamcast_boot_memory(program);
    if (!loaded) {
        return Result<DreamcastBootRunResult>::failure(loaded.error, loaded.detail);
    }

    auto cfg = build_sh4_cfg(program.bytes,
                             loaded.value.load_address,
                             loaded.value.entry_pc);
    if (!cfg) {
        return Result<DreamcastBootRunResult>::failure(cfg.error, cfg.detail);
    }

    DreamcastBootRunResult result{};
    result.memory = std::move(loaded.value);
    result.state = initial_state;
    result.state.pc = result.memory.entry_pc;

    if (!cfg.value.unsupported_sites.empty()) {
        const auto address = cfg.value.unsupported_sites.front();
        auto raw = read_dreamcast_u16(result.memory, address);
        if (!raw) {
            return Result<DreamcastBootRunResult>::failure(raw.error, raw.detail);
        }
        result.stop_reason = DreamcastBootStopReason::unsupported_opcode;
        result.unsupported_address = address;
        result.unsupported_raw = raw.value;
        return Result<DreamcastBootRunResult>::success(std::move(result));
    }

    auto ir = lift_sh4_cfg(cfg.value);
    if (!ir) {
        return Result<DreamcastBootRunResult>::failure(ir.error, ir.detail);
    }

    DreamcastReferenceBus bus(result.memory);
    DreamcastSystemAsic system_asic;
    bus.attach_device(DreamcastBusRegion::system_asic, system_asic);
    const auto irq_boundary_hook = make_dreamcast_system_irq_boundary_hook(system_asic);

    auto run = execute_sh4_ir_reference(ir.value,
                                        result.state,
                                        bus,
                                        max_blocks,
                                        irq_boundary_hook);
    if (!run) {
        if (bus.last_fault().has_value()) {
            result.stop_reason = DreamcastBootStopReason::unmapped_bus_access;
            result.bus_fault = bus.last_fault();
            return Result<DreamcastBootRunResult>::success(std::move(result));
        }
        return Result<DreamcastBootRunResult>::failure(run.error, run.detail);
    }

    result.blocks_executed = run.value.blocks_executed;
    result.operations_executed = run.value.operations_executed;
    switch (run.value.stop_reason) {
        case Sh4ReferenceStopReason::end_of_stream:
            result.stop_reason = DreamcastBootStopReason::end_of_program;
            break;
        case Sh4ReferenceStopReason::left_program:
            result.stop_reason = DreamcastBootStopReason::left_program;
            break;
        case Sh4ReferenceStopReason::block_limit:
            result.stop_reason = DreamcastBootStopReason::block_limit;
            break;
    }
    return Result<DreamcastBootRunResult>::success(std::move(result));
}

}
