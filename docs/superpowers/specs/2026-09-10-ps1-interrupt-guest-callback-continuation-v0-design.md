# PS1 Interrupt Guest Callback Continuation v0 — Design

Status: approved design, implementation not started
Date: 2026-09-10
Branch: `feature/ps1-gp0-dma2-bios-frontier-v0`
Design base: `f485a6fa3434f05505205589a5aa181f055d3308`

## 1. Goal

Advance the JoJo PS1 boot frontier past the first accepted hardware interrupt without importing a retail BIOS, without executing proprietary BIOS ROM code, and without treating the missing post-BIOS exception vector at `0x80000080` as guest NOP code.

The v0 architecture adds an HLE interrupt dispatcher that reproduces only the kernel control-flow contract already evidenced by the commercial checkpoint:

- accepted IRQ enters the PS1 exception path;
- the default kernel exception dispatcher walks the four `SysEnqIntRP` priority chains;
- registered FIRST/SECOND handlers execute as ordinary guest R3000A code;
- guest callbacks may use the already-supported BIOS HLE and MMIO surfaces;
- `B0:17 ReturnFromException` ends interrupt handling and restores the interrupted guest context;
- if all priority chains finish normally, the active `HookEntryInt` jmp-buffer continuation runs before final `ReturnFromException`.

This milestone exists to expose the next real game frontier after IRQ dispatch. It is not a general BIOS replacement.

## 2. Evidence and root cause

The checkpoint produced by build `f485a6fa3434f05505205589a5aa181f055d3308` retired 816,487 instructions and reported two apparent dependencies: BIOS `A0:35` and a byte write of `0x01` to `0x1F801801`.

The `A0:35` frontier is not accepted as genuine game behavior. The preceding checkpoint stopped when the first interrupt was accepted at 816,359 retired instructions. In the new run, the root node stops at `A0:35` at cumulative retired count 816,367: exactly eight instructions later. The address span from the PS1 exception vector `0x80000080` to the BIOS A0 vector alias `0x800000A0` is exactly 0x20 bytes, or eight 32-bit instructions.

The active runtime has no retail BIOS image and the low RAM exception-vector region is zero-filled unless guest code writes it. Continuing raw execution after an accepted interrupt therefore executes eight zero words as NOPs and falls into `0x800000A0`. A residual `r9=0x35` is then misclassified as a real A0 BIOS call.

The subsequent `0x1F801801 = 0x01` dependency is reached only after diagnostic fallback from that false A0 frontier. All fallback variants converge to the same state. It is therefore not evidence sufficient to expand CD-ROM command behavior in this milestone.

Earlier commercial evidence already shows the game registering interrupt infrastructure:

- `C0:02 SysEnqIntRP(priority=2, struc=0x80098D88)`;
- repeated `B0:19 HookEntryInt(0x800616F0)` calls.

The current HLE already stores priority-chain heads and the HookEntryInt address, but the runtime does not execute those chains after an accepted interrupt.

## 3. External behavior being modeled

The PS1 kernel exception handler uses four priority chains, processed in priority order 0 through 3. A `SysEnqIntRP` node has this layout:

- `+0x00`: pointer to next node, zero for end;
- `+0x04`: SECOND function pointer, zero for none;
- `+0x08`: FIRST function pointer, zero for none;
- `+0x0C`: unused by this v0.

For each node, FIRST executes first. If FIRST returns with `r2 == 0`, SECOND is skipped. If FIRST returns with `r2 != 0` and SECOND is non-zero, SECOND executes. Normal return from either callback continues kernel dispatch. A callback may instead invoke `B0:17 ReturnFromException`, which aborts the remaining lower-priority dispatch and returns to the interrupted program.

`HookEntryInt(addr)` points to a 0x30-byte setjmp-like register buffer. If the exception dispatcher reaches its normal end, the kernel restores the saved callee-saved register set from that buffer and resumes at the stored RA/PC with `r2 = 1`. Code resumed through this hook is expected eventually to call `ReturnFromException`.

The R3000A reference executor already implements architectural exception entry and the `RFE` instruction. This milestone does not duplicate those CPU semantics.

## 4. Architecture

### 4.1 New component: `Ps1InterruptContinuation`

Add a dedicated core component under `src/core/` that owns only interrupt-dispatch orchestration state. `Ps1BootRuntime` remains the execution loop and `Ps1HleBios` remains the logical BIOS state owner.

The continuation state must be deterministic and copyable because `Ps1BootRuntime` is copied by MAX3 exploration. At minimum it tracks:

- inactive/active state;
- current dispatch phase;
- priority index 0..3;
- current IntRP node address;
- current node's next/FIRST/SECOND addresses while that node is being processed;
- whether FIRST returned non-zero;
- which guest callback is currently active;
- saved interrupted guest context needed by ReturnFromException;
- whether HookEntryInt restoration is pending/active;
- a bounded/cycle-safe set or equivalent guard for malformed linked chains.

No heap-owned opaque coroutine or host stack continuation is allowed. The state must be ordinary value state suitable for deterministic hashing and MAX3 copies.

### 4.2 Default-vector interception rule

After `step_r3000a` returns an accepted `interrupt` exception with vector PC `0x80000080`, the runtime decides whether to use the HLE default dispatcher.

The HLE dispatcher is entered only when all four 32-bit words at guest addresses `0x80000080`, `0x80000084`, `0x80000088`, and `0x8000008C` read successfully and are zero.

This all-zero pattern means the runtime's post-BIOS environment has no materialized default kernel vector. It must not be executed as eight synthetic NOPs.

If any of those four words is non-zero, the runtime does not intercept the vector. Execution continues at `0x80000080` as ordinary guest code, preserving game-installed exception-vector patches.

If reading the vector itself fails, preserve strict behavior and stop through the existing CPU/MMIO diagnostic path; do not silently enter HLE.

The v0 HLE interception applies only to the normal RAM exception vector `0x80000080`. BEV/ROM-vector handling is out of scope.

### 4.3 Interrupted-context capture

The reference executor performs pending-load retirement before taking an interrupt, then updates COP0 exception state and redirects PC to the exception vector. ReturnFromException must restore the architectural state that the BIOS would have saved before dispatch, while preserving the already-retired delayed load.

Immediately before each guest CPU step, the runtime may retain a candidate pre-step snapshot. If that step returns an interrupt exception, the continuation constructs its saved interrupted context from:

- post-exception GPR/HI/LO values, so a delayed load retired by exception entry stays retired;
- pre-exception Status value from the pre-step snapshot;
- interrupted PC from COP0 EPC;
- ordinary next-PC state corresponding to the interrupted non-delay-slot instruction;
- no active branch delay slot;
- no pending delayed load;
- the remaining CPU state needed for deterministic restoration.

Cause is not restored by ReturnFromException. External interrupt lines continue to be synchronized from I_STAT/I_MASK by the existing runtime loop after restoration.

Interrupts are already accepted only outside an active branch delay slot, so v0 does not need to synthesize a delay-slot resume case.

### 4.4 Access to priority-chain heads

`Ps1HleBios` remains the owner of the four `interrupt_priority_heads_` entries populated by `C0:02/C0:03`.

Expose a read-only query such as `interrupt_priority_head(priority)` for the continuation engine. Do not move chain ownership into `Ps1BootRuntime`, and do not duplicate the list.

The existing SysEnqIntRP/SysDeqIntRP behavior remains unchanged.

### 4.5 Priority-chain traversal

When HLE dispatch starts:

1. begin at priority 0;
2. load the current head from `Ps1HleBios`;
3. for each non-zero node, read `next`, `SECOND`, and `FIRST` as guest 32-bit values from offsets `+0`, `+4`, and `+8`;
4. execute FIRST if non-zero;
5. if FIRST returns normally with `r2 != 0`, execute SECOND if non-zero;
6. after the node completes normally, continue with its captured `next` pointer;
7. when a chain ends, advance to the next priority;
8. after priority 3 ends, enter HookEntryInt completion behavior.

The node's `next` pointer is captured before running callbacks. A callback changing or dequeuing the current list cannot retroactively change which successor this invocation uses. This keeps the traversal deterministic and matches the kernel's call-oriented behavior.

Malformed list handling is strict. A node that cannot be read, an invalid callback fetch, or a cycle/repeated node that exceeds the continuation's guard terminates through an existing diagnostic stop path rather than hanging or fabricating a result.

### 4.6 Guest callback invocation

FIRST and SECOND are executed by the ordinary `step_r3000a` loop. They are not interpreted by special host-side callback code.

To obtain a normal function return without allocating executable guest code, the runtime reserves a synthetic, unmapped guest PC as an internal return sentinel. Before entering a callback:

- set `cpu.pc` to the guest callback address;
- set `cpu.next_pc = callback + 4`;
- clear delay-slot state;
- set `r31/ra` to the synthetic callback-return sentinel;
- preserve all other guest registers as they currently stand;
- force `r0 = 0`.

The runtime checks for the sentinel before attempting a bus fetch. Reaching it means the callback returned normally via `jr ra`. The continuation consumes the callback's resulting `r2` and advances its state machine.

The sentinel must be in a guest address range that `Ps1MemoryBus::guest_to_physical` does not map, and runtime interception must occur only while an interrupt continuation is active. Outside an active continuation, reaching that address remains an ordinary unsupported CPU address.

Callbacks may call already-supported A0/B0/C0/SYS HLE functions and may perform ordinary supported MMIO. Existing stop-reason classification remains authoritative for unsupported CPU, BIOS, GPU, CD-ROM, or MMIO behavior encountered inside a callback.

### 4.7 `B0:17 ReturnFromException`

Add explicit HLE recognition for `B0:17`.

`Ps1HleBios` must not own or restore the runtime's interrupted CPU snapshot. Instead its dispatch result gains an explicit signal that ReturnFromException was requested. `Ps1BootRuntime` consumes that signal.

When an interrupt continuation is active, ReturnFromException:

- aborts remaining FIRST/SECOND handlers and lower-priority chains;
- restores the saved interrupted guest GPR/HI/LO and pre-exception Status;
- restores PC to the interrupted instruction address and next PC to the normal successor;
- clears delay-slot and pending-load continuation artifacts;
- deactivates the interrupt continuation;
- leaves Cause diagnostic state intact;
- lets the next runtime loop synchronize the external IP lines from I_STAT/I_MASK.

If the originating device has not been acknowledged, re-enabling interrupts may cause the hardware interrupt to be accepted again. The runtime must not auto-clear CD-ROM I_STAT/HINTSTS merely to avoid re-entry.

Calling B0:17 when no interrupt continuation is active remains strict/unsupported in v0; it is not generalized into a thread/TCB BIOS implementation.

### 4.8 Normal end and HookEntryInt

If all four priority chains complete without any callback requesting ReturnFromException, use the current `Ps1HleBios::interrupt_hook_address()` state.

#### No active custom hook

If no hook is installed, or if the stored hook is the known HLE default structure produced by `ResetEntryInt`, finish the interrupt continuation by applying the same saved-context restoration as ReturnFromException. Do not jump into the unmaterialized retail BIOS ReturnFromException body at physical `0x00000F40`.

#### Guest HookEntryInt buffer

For a guest hook address, read the 0x30-byte structure already documented by the kernel ABI:

- `+0x00`: RA/resume PC;
- `+0x04`: SP;
- `+0x08`: FP;
- `+0x0C..+0x28`: R16..R23;
- `+0x2C`: GP/R28.

Restore only those ABI fields, set `r2 = 1`, set PC to the restored RA/resume PC, set next PC to PC+4, clear delay-slot/pending-load state, and mark the continuation as hook-active.

The original interrupted context remains saved while hook code runs. A later `B0:17 ReturnFromException` from that hook performs the final restore to the interrupted program.

If the hook buffer is unreadable, stop strictly. Do not guess its contents.

### 4.9 No nested exception continuations

The PS1 kernel does not support nested exception handling in this model. While an interrupt continuation is active, accepting a second interrupt as a new continuation is an explicit terminal boundary in v0.

Ordinary non-interrupt exceptions generated by callback code continue to use the existing CPU diagnostic behavior; this milestone does not add general nested exception dispatch.

### 4.10 MAX3 state identity

`Ps1BootRuntime::diagnostic_state_hash()` must include all continuation state that can change future execution, including:

- active/inactive flag;
- dispatch phase;
- priority and current node;
- captured next/FIRST/SECOND pointers;
- callback phase/return state;
- hook phase;
- saved interrupted-context values needed for restoration;
- cycle-guard state if it influences future traversal.

Two MAX3 nodes at different positions in interrupt dispatch must not deduplicate merely because their visible CPU and bus state temporarily matches.

## 5. Reporting and diagnostics

No new public checkpoint format is required in v0.

Existing diagnostics are sufficient:

- `interrupts_accepted` records hardware interrupt acceptance;
- recent trace records guest callback PCs;
- BIOS events record BIOS calls issued by callbacks/hook code;
- MMIO events expose device acknowledgements or new hardware frontiers;
- existing stop reasons classify unsupported operations.

The key regression requirement is that a zero-filled default exception vector must no longer produce a fake BIOS A0 event at `0x800000A0` solely by sequential fallthrough.

## 6. Strictly out of scope

This milestone does not implement or relax any of the following:

- `A0:35 lsearch`;
- any new CD-ROM command or response-FIFO behavior;
- automatic CD-ROM interrupt acknowledge or HINTSTS clear;
- `B0:07 DeliverEvent` callback execution;
- general event callback dispatch;
- generic `setjmp/longjmp` implementation beyond consuming the already-recorded HookEntryInt buffer;
- materialization or execution of a retail BIOS ROM;
- C0 default interrupt-handler implementations not already represented by registered guest IntRP nodes;
- DMA3/CD sector transfer;
- GPU drawing, linked-list DMA2, renderer, VRAM presentation, or frame output;
- nested kernel exceptions;
- native x64 lowering.

If the first real callback reaches any of these boundaries, the runtime must stop and report it rather than speculate.

## 7. Required TDD coverage

Implementation must proceed RED → GREEN and include synthetic-only fixtures for all tests.

1. **False A0 regression:** accepted interrupt + all-zero `0x80000080..8C` + residual `r9=0x35` must not generate an A0:35 BIOS frontier after eight instructions.
2. **Custom vector preservation:** any non-zero word in the four-word exception vector disables HLE interception and guest execution begins at `0x80000080`.
3. **Chain order:** synthetic IntRP nodes across priorities execute in priority order 0,1,2,3 and linked-list order within a priority.
4. **FIRST zero:** FIRST normal return with `r2=0` skips SECOND and advances to the next node.
5. **FIRST non-zero:** FIRST normal return with `r2!=0` invokes SECOND.
6. **Callback normal return:** `jr ra` reaches the internal sentinel and resumes dispatcher state rather than becoming an unsupported-address boundary.
7. **Callback BIOS/MMIO:** a synthetic callback can perform one already-supported BIOS call and one supported MMIO operation before returning.
8. **Early ReturnFromException:** a synthetic FIRST or SECOND invoking B0:17 restores the interrupted context and skips remaining/lower-priority nodes.
9. **Context fidelity:** interrupted GPR/HI/LO, pre-exception Status, PC, and a pending load retired at interrupt entry are restored with the documented v0 semantics.
10. **HookEntryInt restore:** after normal chain exhaustion, a synthetic guest jmp buffer restores RA, SP, FP, R16..R23, GP and sets `r2=1`; hook guest code then executes normally.
11. **Hook B0:17:** hook code invoking B0:17 returns to the original interrupted PC/context.
12. **Default/no-hook completion:** absence of a guest hook, or the known ResetEntryInt default structure, finishes through direct saved-context restoration without executing address `0x00000F40` as raw zero-filled BIOS code.
13. **Nested interrupt strictness:** a second accepted interrupt while continuation state is active terminates deterministically rather than creating nested continuation state.
14. **Malformed chain strictness:** unreadable IntRP node/callback and a cyclic chain are terminal, not silently skipped.
15. **MAX3 hash:** changing only continuation phase/node/saved context changes the runtime diagnostic hash; identical continuation state hashes deterministically.
16. **CD-ROM non-expansion regression:** the existing unsupported second `0x1F801801 = 0x01` case remains unsupported unless reached through the new real handler path in a future commercial checkpoint.
17. **Cross-platform:** full Linux and Windows/MSVC suites and existing readiness/architecture/disc/UDP gates stay green on the exact final SHA.

## 8. Expected commercial-checkpoint outcome

With this milestone implemented, the next commercial checkpoint should no longer stop at fake `A0:35` caused by zero-vector fallthrough.

The expected next real evidence is one of:

- guest code at the registered priority-2 IntRP FIRST/SECOND callback;
- an IRQ acknowledge sequence, likely involving I_STAT and/or CD-ROM interrupt registers;
- a BIOS call made from the real callback/hook path;
- a new strict CPU/MMIO/device boundary inside that guest handler;
- normal HookEntryInt resume followed by ReturnFromException.

Only evidence from that post-handler path may justify expanding CD-ROM, event, BIOS callback, GPU, or DMA behavior.

## 9. Acceptance criteria

The milestone is complete only when:

- synthetic tests prove the all-zero vector no longer falls through to fake A0;
- registered guest IntRP handlers execute through ordinary R3000A stepping;
- FIRST/SECOND control flow and B0:17 early exit behave deterministically;
- HookEntryInt guest-buffer continuation works for the evidenced ABI subset;
- MAX3 copies/hash include continuation state;
- no new CD-ROM/GPU/DMA/event behavior is introduced without evidence;
- Linux and Windows/MSVC CI are green on the exact same final commit;
- the Windows artifact from that exact commit is verified before asking for the next commercial checkpoint.
