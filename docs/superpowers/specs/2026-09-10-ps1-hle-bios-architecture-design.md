# PS1 HLE BIOS Architecture Design

## Status

Approved architectural direction: replace the growing BIOS logic embedded in `Ps1BootRuntime` with a clean-room, modular `Ps1HleBios` subsystem. The implementation must not embed or require a proprietary Sony PlayStation BIOS ROM.

Baseline branch: `feature/ps1-syscalls-max3-v0`.

Baseline behavior already validated before this migration includes PS1 A0/B0/C0 HLE calls, DICR/Timer1/interrupt-mask register work, MAX3 diagnostic exploration, and PS1 SYS00/SYS01/SYS02 handling. Migration must preserve that behavior exactly unless this spec explicitly extends it.

## Goal

Provide an internal High-Level Emulation BIOS for the PS1 runtime so commercial software can cross documented BIOS/kernel services without executing the original BIOS ROM, while keeping unsupported or hardware-dependent services explicit and observable.

The immediate value is to reduce `bios_call_unimplemented`/`cpu_boundary` checkpoint churn and move commercial boot evidence toward actual hardware dependencies such as GPU, DMA, CD-ROM, timers, IRQ, controller, SPU, and GTE.

## Non-goals

- Do not distribute, embed, derive from, or require Sony BIOS ROM bytes.
- Do not claim full retail-BIOS compatibility in this tranche.
- Do not fabricate successful CD-ROM, GPU, event, thread, memory-card, controller, or filesystem operations whose state machines do not yet exist.
- Do not replace the architectural R3000A exception implementation. The generic executor must continue to model `syscall` as a CPU exception; PS1 BIOS HLE interception remains a platform/runtime policy.
- Do not make diagnostic MAX3 fallback behavior part of the production runtime.
- Do not remove the MAX3 frontier mechanism. It remains the discovery path for genuinely unsupported services.

## Source and compatibility policy

The HLE is clean-room source code based on public behavioral documentation and bounded observations from the user's legally supplied commercial installation. Public PS1 BIOS function descriptions are treated as behavioral contracts, not as source code to copy.

Where public documentation describes revision-specific bugs or ambiguous behavior, the HLE must either:

1. implement the behavior required by an observed commercial call with a regression test, or
2. leave that service unsupported until its exact compatibility requirement is known.

No handler may silently return success merely because a function number is known.

## Architecture

### `Ps1HleBios`

Create a focused component:

- `src/core/ps1_hle_bios.h`
- `src/core/ps1_hle_bios.cpp`

`Ps1HleBios` owns BIOS/kernel guest-visible HLE state and dispatches four call domains:

- A0 table (`PC` physical `0x000000A0`, selector in `R9/T1`)
- B0 table (`0x000000B0`, selector in `R9/T1`)
- C0 table (`0x000000C0`, selector in `R9/T1`)
- SYS calls (`syscall` opcode, selector in `R4/A0`)

It must not own the R3000A executor, disc parser, MAX3 explorer, GPU, DMA, or CD-ROM implementation.

### Dispatch contract

Use explicit request/result types rather than a large boolean handler:

```cpp
namespace jojo {

enum class Ps1HleBiosDomain : std::uint8_t {
    a0,
    b0,
    c0,
    sys,
};

struct Ps1HleBiosCall {
    Ps1HleBiosDomain domain{};
    std::uint32_t selector{};
    std::uint32_t pc{};
    std::uint32_t a0{};
    std::uint32_t a1{};
    std::uint32_t a2{};
    std::uint32_t a3{};
    std::uint32_t ra{};
};

enum class Ps1HleBiosDisposition : std::uint8_t {
    handled,
    unsupported,
    terminal,
};

struct Ps1HleBiosResult {
    Ps1HleBiosDisposition disposition{Ps1HleBiosDisposition::unsupported};
};

class Ps1HleBios {
public:
    [[nodiscard]] Ps1HleBiosResult dispatch(
        const Ps1HleBiosCall& call,
        R3000aState& cpu) noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
};

} // namespace jojo
```

The exact names above are normative for the first implementation plan unless a compiler/type conflict forces a mechanical adjustment.

### State ownership

Move the current BIOS-specific persistent state out of `Ps1BootRuntime` and into `Ps1HleBios`:

- heap initialization state (`base`, `size`)
- interrupt hook address
- PAD/card auto-ack state
- root-counter/VBlank auto-ack state for indices 0..3
- logical ISO9660-remove state

SYS01/SYS02 critical-section state remains represented by the guest CPU COP0 status register; do not duplicate it inside `Ps1HleBios`.

`Ps1BootRuntime` retains CPU, PS1 memory bus, diagnostic frontier state, and policy checks that decide whether a platform-specific HLE interception is safe before dispatch.

The HLE-owned persistent state must participate in `diagnostic_state_hash()` so MAX3 never deduplicates two branches whose BIOS state differs.

## Return mechanisms

A0/B0/C0 and SYS do not share the same return mechanism.

### A0/B0/C0 return

A handled A0/B0/C0 service performs the BIOS-call return used by the current runtime:

- set `PC = R31/RA`;
- set `next_pc = PC + 4`;
- clear delay-slot state;
- preserve architectural zero register semantics.

Handlers may modify documented return registers before this return.

### SYS return

A handled SYS service represents the platform HLE completion of a `syscall` instruction, not an A0/B0/C0 jump-table return. It must preserve the already-tested runtime behavior:

- retire any pending delayed load exactly once before applying the SYS service;
- apply the SYS selector's documented register/COP0 effects;
- advance from the syscall instruction to its sequential continuation (`PC = next_pc`, then advance `next_pc` by four);
- clear delay-slot state;
- preserve `R0 = 0`.

`Ps1BootRuntime` remains responsible for pre-dispatch safety checks such as rejecting HLE interception when the syscall is in a delay slot or when an interrupt would preempt it. `Ps1HleBios` owns the selector-specific SYS behavior once dispatch is authorized.

This distinction is normative: do not reuse the A0/B0/C0 `return via RA` helper for SYS.

## Runtime data flow

### A0/B0/C0

1. `Ps1BootRuntime::run()` observes PC at one of the BIOS vectors.
2. It records the BIOS call in the existing `Ps1BootReport` history.
3. It builds a `Ps1HleBiosCall` from table, selector, arguments, and RA.
4. It calls `Ps1HleBios::dispatch()`.
5. `handled`: the HLE updates guest state and performs the A0/B0/C0 return mechanism.
6. `unsupported`: runtime emits `bios_call_unimplemented` and exposes a MAX3 frontier.
7. `terminal`: runtime stops with a distinct terminal BIOS reason once such a service is introduced; this tranche need not add a terminal service unless required by a migrated handler.

### SYS

The generic R3000A executor continues to raise the architectural syscall exception whenever it executes a syscall normally.

For PS1 runtime HLE, before invoking the generic executor for the current instruction, `Ps1BootRuntime` may intercept a SYS service only when all of these are true:

- the observed opcode is `syscall`;
- the call is not in a delay slot;
- no enabled pending interrupt would preempt that instruction;
- the selector is one handled by `Ps1HleBios`;
- the platform-specific service can be applied without executing original BIOS ROM code.

If those conditions are not all satisfied, the runtime falls through to the generic R3000A executor and retains normal architectural exception/boundary behavior.

SYS services must retain the already-tested PS1 semantics for SYS00/SYS01/SYS02. SYS03 and SYS04+ remain unsupported in this tranche because they depend on thread/event behavior.

## First migration tranche

### Existing observed handlers

Migrate without semantic changes:

- A0/0x39 `InitHeap`
- A0/0x56 `_96_remove`
- A0/0x72 `_96_remove` alias
- B0/0x19 `HookEntryInt`
- B0/0x5B `ChangeClearPAD`
- C0/0x0A `ChangeClearRCnt`
- SYS00 `NoFunction`
- SYS01 `EnterCriticalSection`
- SYS02 `ExitCriticalSection`

The old public accessors used by tests may temporarily remain as forwarding accessors on `Ps1BootRuntime` during migration, but the source of truth must become `Ps1HleBios`.

### Safe documented no-op/return-zero expansion

The first broad HLE batch may additionally handle only functions publicly documented as no-function/return-zero, with explicit table-driven tests for every accepted selector:

- C0 selectors `0x0E`, `0x0F`, `0x10`, `0x11`, `0x14`: return `R2/V0 = 0` and return normally.
- A0 selectors `0x57..0x5A`, `0x73..0x77`, `0x79..0x7B`, `0x7D`, `0x7F..0x80`, `0x82..0x8F`, `0xB0..0xB1`, `0xB3`: return `R2/V0 = 0` and return normally.

This list is closed. Do not broaden it from ranges described as `N/A jump_to_00000000h`; those functions are not equivalent to a successful no-op.

### Explicitly unsupported in this tranche

Remain frontier-producing until their backing state machines exist or commercial evidence justifies a bounded implementation:

- SYS03 `ChangeThreadSubFunction`
- SYS04+ event delivery path
- event creation/delivery/open/close/wait/test families
- thread creation/change/close families
- file/device I/O that requires DCB/FCB semantics
- CD-ROM functions that require real command/data timing
- memory-card services
- GPU helper services that require GPU command execution
- interrupt-chain mutation beyond the already-observed hook/clear behavior
- kernel functions whose documented behavior intentionally jumps to address zero
- system-error/hang paths unless a dedicated terminal stop contract is introduced

## Internal organization

Keep `ps1_hle_bios.cpp` small enough to reason about. Dispatch may be split into focused helpers in the same translation unit for this tranche:

- `dispatch_a0`
- `dispatch_b0`
- `dispatch_c0`
- `dispatch_sys`

If a later family becomes stateful and substantial (events, threads, files), it must become its own component rather than growing the BIOS dispatcher indefinitely.

The HLE must not call platform APIs or perform host filesystem access. It operates only on guest-visible state passed through explicit interfaces.

## MAX3 integration

MAX3 remains diagnostic-only.

- A service handled by `Ps1HleBios` is no longer a MAX3 dependency.
- Unsupported A0/B0/C0 calls remain branchable with the existing four diagnostic return policies.
- Unsupported SYS calls are not automatically branchable in this tranche; the runtime keeps them as a CPU boundary unless a future design defines safe SYS fallback semantics.
- `Ps1HleBios::diagnostic_state_hash()` is combined into `Ps1BootRuntime::diagnostic_state_hash()`.
- The current MAX3 limits remain unchanged: `max_nodes=5461`, `max_branch_depth=6`, `max_total_retired=1000000000`, per-segment `UINT64_MAX`, `trace_capacity=131072`, `mmio_event_capacity=65536`, `bios_event_capacity=65536`, and stagnation limit `2000000`.

## Error and unsupported-service behavior

Unknown service numbers must not mutate guest state.

For an unsupported A0/B0/C0 call:

- preserve current CPU register values;
- return `unsupported`;
- let `Ps1BootRuntime` record the structured frontier;
- MAX3 may snapshot and branch from that exact state.

For an unsupported SYS call:

- do not apply SYS HLE mutation;
- let the generic R3000A executor retain the architectural syscall exception/boundary behavior;
- do not fabricate an A0/B0/C0-style return.

A handler must be deterministic for identical input CPU/HLE state.

## Testing strategy

All implementation remains TDD RED -> GREEN.

### Unit tests

Add `tests/test_ps1_hle_bios.cpp` covering:

- domain/selector dispatch separation
- migration of every currently supported BIOS service
- all safe return-zero selectors in the closed list
- unknown selector leaves CPU/HLE state unchanged and reports `unsupported`
- A0/B0/C0 return via RA
- SYS00 register preservation and sequential return
- SYS01 return value, COP0 transition, and sequential return
- SYS02 COP0 transition, required register preservation, and sequential return
- state hash changes when HLE-owned state changes
- copied HLE objects produce identical state hashes until mutated

### Runtime regression tests

Existing tests must remain green. Add integration assertions that:

- `Ps1BootRuntime` delegates A0/B0/C0 to `Ps1HleBios`;
- an unsupported BIOS call still creates the same MAX3 frontier;
- SYS03 still produces a boundary through the generic executor;
- generic `step_r3000a()` syscall exception tests remain unchanged and green;
- MAX3 dedup remains sensitive to HLE state;
- pending-load and interrupt-preemption SYS guards remain green after migration.

### Commercial evidence gate

The next Windows checkpoint after this migration must:

- still begin with `format=jojo-max3-checkpoint-v1`;
- not regress to any migrated A0/B0/C0/SYS service as an unsupported frontier;
- continue from the current commercial frontier after SYS02.

Commercial evidence is diagnostic only and must not be committed to the repository.

## CI and acceptance

Acceptance requires one exact final commit SHA with:

- Linux configure/build/readiness/PS1 architecture/tests/revision/UDP gates green;
- Windows x64 MSVC 2022 Release build/readiness/PS1 architecture/tests/revision/UDP gates green;
- Windows executable artifact uploaded;
- no proprietary BIOS/game data added;
- diff review confirming changes are restricted to HLE BIOS architecture, migration, tests, and required build registration.

## Follow-on architecture

After this first tranche is green and commercial MAX3 evidence is collected, extend the BIOS in dependency-driven families rather than random individual selectors:

1. events and interrupt-chain primitives;
2. thread/TCB primitives;
3. heap allocation beyond `InitHeap`;
4. file/device layer;
5. CD-ROM BIOS services backed by the real CD-ROM device model;
6. controller/card services backed by their real device models.

Each stateful family receives its own design if it introduces a new subsystem. The BIOS dispatcher remains an orchestration layer, not the implementation home for every PS1 device.