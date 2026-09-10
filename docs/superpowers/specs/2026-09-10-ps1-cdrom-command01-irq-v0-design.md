# PS1 CD-ROM Command 01 + IRQ Bridge v0 Design

Date: 2026-09-10
Branch: `feature/ps1-gp0-dma2-bios-frontier-v0`
Approved direction: hardware-oriented minimal CD-ROM state, command `0x01` only, plus interrupt bridge.

## Context and evidence

The commercial MAX3 checkpoint `m3a-checkpoint(20260910-093208).txt` completed with one node and stopped by diagnostic stagnation after 2,816,359 retired instructions. The only remaining dependencies are 8-bit accesses at:

- write `0x1F801800` (CD-ROM bank/index select),
- read/write `0x1F801803`,
- write `0x1F801801`.

The observed sequence is:

1. write `0x01` to `0x1F801800`,
2. read `0x1F801803`,
3. write `0x00` to `0x1F801800`,
4. write `0x00` to `0x1F801803`,
5. write `0x00` to `0x1F801800`,
6. write `0x01` to `0x1F801801`.

No CD-ROM command is currently recorded because all six accesses still pass through diagnostic MMIO shadowing. The run reports zero accepted interrupts, zero DMA transfers, zero VRAM writes, and zero presented frames.

Hardware reference used for semantics: psx-spx CD-ROM Drive and Interrupts documentation. `0x1F801800` selects one of four register banks. At bank 1, read `0x1F801803` is HINTSTS. At bank 0, write `0x1F801801` is COMMAND. Command `0x01` is Nop and returns one status byte with INT3/Acknowledge. CD-ROM is IRQ2 in I_STAT, while the aggregate enabled interrupt condition reaches COP0 IP2.

## Goals

Implement the smallest real CD-ROM subsystem that can replace the four speculative MMIO dependencies above without turning unrelated accesses into permissive no-ops.

The subsystem must:

- model register bank selection;
- model HINTSTS sufficiently for INT3;
- model a response FIFO sufficient for command `0x01`;
- implement only command `0x01`;
- expose deterministic command diagnostics to `Ps1BootReport`;
- raise the CD-ROM source in I_STAT when the CD-ROM interrupt condition becomes active;
- synchronize `(I_STAT & I_MASK) != 0` to the R3000A external IP2 input;
- participate in MAX3 diagnostic state hashing;
- keep unsupported commands, widths, and unrelated CD-ROM register behavior strict.

## Non-goals

This milestone does not implement:

- `Setloc`, `ReadN`, `ReadS`, `Play`, `Pause`, `Init`, `GetID`, or other CD-ROM commands;
- CD-ROM sector reads or DMA3;
- XA/CD-DA audio;
- timing-accurate HC05 command latency;
- asynchronous multi-response command queues beyond what command `0x01` requires;
- parameter FIFO semantics beyond storage scaffolding if needed by the component boundary;
- GPU, DMA2, rendering, or presentation changes;
- a native x64 recompiler backend.

## Component boundary

Add a dedicated `Ps1CdromState` component under `src/core/` rather than continuing to grow `Ps1MemoryBus` with device-specific state.

`Ps1CdromState` owns:

- current bank/index (`0..3`),
- drive status byte,
- interrupt enable/mask byte,
- interrupt status/type (`INT0..INT7` low bits),
- response FIFO state,
- command count,
- deterministic command event history or equivalent state sufficient for report extraction,
- previous CD-ROM IRQ-line level for edge detection.

`Ps1MemoryBus` owns the top-level interrupt controller state (`I_STAT`, `I_MASK`) and contains one `Ps1CdromState`. It routes only the approved 8-bit CD-ROM accesses into the component.

`Ps1BootRuntime` remains responsible for copying aggregate interrupt state into `R3000aState::external_interrupt_pending` and for producing the boot report.

## CD-ROM register behavior in v0

### `0x1F801800` write — bank/index select

All 8-bit writes use `value & 3` as the selected bank. Other status bits are not writable.

This access is real MMIO and must no longer produce a diagnostic probe.

### `0x1F801803` read

For bank 1 (and bank 3 if reached through the same HINTSTS mirror), return HINTSTS. The visible value is `0xE0 | interrupt_type`, matching the reserved high bits documented as one.

The currently observed pre-command state therefore has interrupt type zero.

For bank 0/2, HINTMSK reads remain strict in this milestone unless required internally by a test of the approved IRQ bridge. The checkpoint does not currently require guest-visible HINTMSK reads.

### `0x1F801803` write

For bank 0, accept only the observed `0x00` HCHPCTL write as a no-effect valid hardware write. Non-zero HCHPCTL behavior remains strict.

Bank 1 HCLRCTL behavior is intentionally deferred until a checkpoint observes it. This prevents predicting the guest's acknowledgement sequence before evidence exists.

### `0x1F801801` write at bank 0 — COMMAND

Command `0x01` is the only supported command.

On command `0x01`:

1. increment the real CD-ROM command counter;
2. record a diagnostic command event with command `0x01`, current bank, and current status;
3. push the current drive status byte into the response FIFO;
4. set HINTSTS interrupt type to INT3/Acknowledge;
5. evaluate the CD-ROM interrupt source edge.

Any other command remains unsupported/terminal rather than silently succeeding.

### `0x1F801801` read — RESULT

Although this read is not yet a checkpoint dependency, it is part of the approved command `0x01` response contract. Reading RESULT returns the queued status byte and advances the response FIFO. It is implemented only to the extent necessary for the one-byte Nop response; broader FIFO padding/replay behavior is deferred.

## Synthetic post-BIOS state

The active runtime starts from a loaded PS-X EXE rather than executing the complete retail BIOS boot path. Therefore the CD-ROM component distinguishes hardware-reset defaults from a synthesized post-BIOS executable-entry state.

For `Ps1BootRuntime::create`, seed:

- drive status = `0x02` (spindle motor on, shell closed, no error/seek/read/play flag),
- CD-ROM interrupt enable = `0x1F` provisionally as the post-BIOS executable-entry contract.

The `0x02` status is consistent with an executable already loaded from a closed, spinning disc. The `0x1F` interrupt-enable value is a deliberate bootstrap assumption, not direct checkpoint evidence; psx-spx documents `0x1F` as the typical value enabling all CD-ROM interrupt types. The assumption must stay isolated in the runtime bootstrap so it can be revised without changing hardware-reset semantics.

A standalone `Ps1CdromState` or `Ps1MemoryBus` does not claim `0x1F` as a raw hardware-reset value.

## Interrupt-controller bridge

CD-ROM's device IRQ condition is active when `(cdrom_interrupt_enable & cdrom_hintsts_low5) != 0`.

When that device condition transitions from false to true, set I_STAT bit 2 (CD-ROM IRQ2). I_STAT remains latched until guest acknowledgement through the already modeled I_STAT write semantics.

The R3000A sees the PS1 interrupt controller as a single external hardware input on COP0 IP2. Before each CPU step, `Ps1BootRuntime` synchronizes only bit `0x04` of `external_interrupt_pending` from `(I_STAT & I_MASK) != 0`, preserving any other external pending bits.

To make a real interrupt handler able to inspect the controller, implement native-width `read16(I_STAT)`. `read32(I_STAT)` remains strict because that broader access was explicitly kept unobserved in the previous milestone.

This bridge does not force an interrupt if I_MASK bit 2 is disabled. It merely models the hardware path. Whether the commercial path enables CD-ROM in I_MASK is left to subsequent evidence.

## Diagnostic reporting

`Ps1BootReport::cdrom_command_count` must report the number of real CD-ROM commands observed during the run, not diagnostic shadow writes.

`recent_cdrom_commands` must receive a `Ps1CdromCommandSummary` for each supported command event retained within the existing report capacity strategy. For command `0x01`, the summary contains command `0x01`, bank/index 0, and the status byte returned by the command.

A real CD-ROM command counts as diagnostic progress for stagnation tracking so the runtime does not immediately classify successful new device activity as a stall.

## MAX3 hashing

`Ps1MemoryBus::diagnostic_state_hash()` must include all guest-observable CD-ROM state relevant to future execution:

- bank/index,
- drive status,
- interrupt-enable value,
- interrupt status/type,
- response FIFO contents/read position,
- command counter and any retained state needed to distinguish command outcomes,
- CD-ROM IRQ-line edge state,
- I_STAT/I_MASK as already applicable.

This prevents MAX3 from deduplicating nodes whose CD-ROM/interrupt state would lead to different execution.

## Strictness rules

The diagnostic probe must no longer fire for the exact approved accesses when they are semantically valid.

Remain unsupported unless separately observed or required by the approved command `0x01` contract:

- CD-ROM commands other than `0x01`,
- 16/32-bit CPU accesses to the CD-ROM control ports,
- non-zero bank-0 HCHPCTL writes,
- bank-1 HCLRCTL writes,
- parameter FIFO writes,
- RDDATA reads,
- DMA3 registers and transfers,
- CD audio/volume banks.

Unsupported device commands should surface as `device_command_unimplemented` where the runtime has enough information to classify them; otherwise they remain structured MMIO failures. No unsupported operation may silently become a no-op.

## TDD and tests

Implementation uses RED -> GREEN and Windows MSVC CI remains authoritative alongside Linux.

Required tests:

1. A unit test reproduces the six exact MMIO events from the commercial checkpoint and verifies no diagnostic probe is emitted for the approved behavior.
2. Before command `0x01`, bank-1 HINTSTS exposes INT0 with the documented high reserved bits.
3. Command `0x01` queues status `0x02`, produces INT3, increments command count exactly once, and records a deterministic command diagnostic.
4. RESULT returns the one-byte status response.
5. The CD-ROM IRQ source sets I_STAT bit 2 only on the device-line rising edge.
6. `read16(I_STAT)` exposes the latched bit; I_STAT acknowledgement clears it with the existing write-zero-to-clear semantics.
7. `(I_STAT & I_MASK) != 0` drives R3000A external IP2; masked CD-ROM IRQ does not preempt the CPU.
8. With COP0 IE + IM2 enabled and I_MASK bit 2 enabled, the CPU accepts the interrupt and reaches the existing exception path.
9. `Ps1BootReport` reports one CD-ROM command and its command/index/status summary.
10. MAX3 state hash changes for meaningful CD-ROM state changes and remains deterministic for identical sequences.
11. An unimplemented CD-ROM command remains strict/terminal.
12. Unobserved CD-ROM widths and non-approved register behaviors remain strict.

## Expected commercial-checkpoint outcome

After this milestone, the four current speculative dependencies should disappear from MAX3 for the observed sequence. The next commercial checkpoint is expected to reveal one of:

- guest acknowledgement of INT3/HCLRCTL,
- RESULT/HSTS polling behavior,
- I_MASK/interrupt-handler behavior,
- the next CD-ROM command,
- or a new subsystem frontier.

No claim is made that this milestone will produce graphics, DMA, sector reads, or a playable build. Its success criterion is narrower: replace the first observed CD-ROM protocol with real deterministic device/IRQ semantics and expose the next evidence frontier.