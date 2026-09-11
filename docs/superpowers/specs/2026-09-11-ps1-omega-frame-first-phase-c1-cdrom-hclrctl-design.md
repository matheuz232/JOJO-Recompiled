# PS1 OMEGA Frame-First — Phase C1 CD-ROM HCLRCTL Design

Date: 2026-09-11
Parent evidence phase: `design/ps1-omega-frame-first-phase-c0`
Production baseline: `feature/ps1-omega-frame-first-consolidation`
Baseline SHA: `be2645471daff4f43d6cae91f15b0b755b04b419`

## 1. Purpose

Phase C1 implements the smallest semantically correct CD-ROM behavior required to cross the first strict blocker observed on the refreshed JoJo checkpoint from the consolidated Phase B artifact.

The blocker is not GPU or DMA. Strict commercial execution reaches a CD-ROM write to `0x1F801803` after selecting bank 1 and stops because the current `Ps1CdromState` rejects that bank/register write.

C1 therefore adds only the HCLRCTL acknowledge behavior required by the observed path. It does not broaden CD-ROM command coverage, data transfer, timing, sector streaming, DMA, GPU, GTE, SPU, SIO, or unrelated interrupt behavior.

## 2. Authoritative commercial evidence

The refreshed checkpoint produced from CI run #1688 / artifact `10195773587` reports:

- `termination_reason=completed` for the MAX3 search;
- `node_count=1`;
- `best_node=0`;
- `best_path_count=0`;
- `dependency_0_kind=terminal_mmio`;
- `dependency_0_address=0x1F801803`;
- `dependency_0_width=1`;
- `dependency_0_write=1`;
- `dependency_0_value=0x07`;
- `node_0_stop_reason=mmio_unimplemented`;
- `instructions_retired=816561`;
- `last_pc=0x8004B494`;
- `last_opcode=0xA0430000`;
- `speculative_mmio_count=0`;
- `mmio_event_0_speculative=0`;
- `gpu_gp0_command_count=1`;
- `gpu_gp1_command_count=3`;
- `cdrom_command_count=1`;
- `interrupts_accepted=1`;
- `dma_transfer_count=0`;
- `vram_write_count=0`;
- `presented_frames=0`.

The final trace sequence contains:

- `0x8004B480: li v0, 1`;
- `0x8004B484: sb v0, 0(v1)`;
- `0x8004B490: li v1, 7`;
- `0x8004B494: sb v1, 0(v0)`.

Together with the terminal address, this is consistent with JoJo selecting CD-ROM bank 1 and then writing `0x07` to `0x1F801803`.

The checkpoint is authoritative for scope selection because the terminal MMIO event is non-speculative and the Phase B `best_*` contract is strict-authoritative.

## 3. Hardware interpretation

Public PS1 hardware documentation identifies `0x1F801803` write with index/bank 1 as HCLRCTL / the CD-ROM interrupt-flag acknowledge register.

For C1, the supported semantics are intentionally limited to bits 0–4:

- bits 0–2: acknowledge/clear the HC05 interrupt status bits;
- bit 3: acknowledge BFEMPT;
- bit 4: acknowledge BFWRDY.

The observed JoJo value is `0x07`, which acknowledges the HC05 interrupt status.

Bits 5–7 are not part of C1 because they have additional device side effects that the current model does not implement:

- bit 5: sound-map/XA-ADPCM clear;
- bit 6: parameter FIFO clear;
- bit 7: decoder/chip reset.

Any HCLRCTL write with one or more of bits 5–7 set remains unsupported in C1. This preserves the project's fail-closed rule rather than pretending those effects succeeded.

## 4. Current implementation gap

`Ps1CdromState::write8()` currently handles `0x1F801803` only for `index_ == 0` with value `0`, treating that case as the existing request-register behavior.

A write to the same physical address while `index_ == 1` falls through to `unsupported_register`.

`Ps1MemoryBus::write8()` correctly routes `0x1F801803` into `Ps1CdromState`, so C1 does not require a new MMIO mapping. The blocker is specifically missing bank-1 device semantics.

## 5. C1 state transition

When `physical == 0x1F801803` and `index_ == 1`:

1. If `value & 0xE0` is non-zero, return `Ps1CdromIoStatus::unsupported_register` and do not mutate CD-ROM state.
2. Otherwise, treat `value & 0x1F` as acknowledge bits.
3. Clear the acknowledged bits from `interrupt_status_` using deterministic bitwise acknowledge semantics.
4. Drain the currently modeled response FIFO (`response_.reset()`) on a supported acknowledge, matching the device-level acknowledge boundary represented by the existing single-response model.
5. Recompute the CD-ROM IRQ line from `interrupt_enable_` and the remaining interrupt status.
6. Return `Ps1CdromIoStatus::ok`.

For the observed JoJo state, `interrupt_status_ == 3` and `value == 0x07`, so the expected result is:

- `interrupt_status_ == 0`;
- `response_` empty;
- `irq_line_ == false`;
- write status `ok`.

The bus-level PS1 `I_STAT` bit already latched by the CD-ROM rising edge is a separate interrupt-controller state. HCLRCTL does not silently clear `Ps1MemoryBus::interrupt_status_`; guest code must acknowledge I_STAT through the existing interrupt-controller register semantics. C1 must preserve that separation.

## 6. Scope boundaries

C1 may modify only the smallest files needed for this transition and its contracts, expected to be:

- `src/core/ps1_cdrom_state.cpp`;
- `tests/test_ps1_cdrom_state.cpp`;
- `tests/test_ps1_memory_bus.cpp`.

`src/core/ps1_cdrom_state.h` should remain unchanged unless implementation proves a new private helper is required; no new public API is planned.

C1 must not add:

- new CD-ROM commands;
- parameter FIFO modeling;
- sound-map/XA playback behavior;
- decoder reset behavior;
- sector/data FIFO behavior;
- CD-ROM timing model changes;
- DMA semantics;
- GPU semantics;
- new BIOS/HLE behavior;
- speculative fallback behavior.

## 7. RED contracts

Implementation follows RED → GREEN.

### 7.1 Device-level RED

Extend `test_ps1_cdrom_state.cpp` with an observed-path contract:

1. seed post-BIOS CD state with drive status `0x02` and interrupt enable `0x1F`;
2. issue existing Getstat command `0x01` in bank 0;
3. verify interrupt status is `3` and IRQ line is asserted;
4. consume the existing response byte through `0x1F801801`;
5. select bank 1 through `0x1F801800 = 1`;
6. write `0x07` to `0x1F801803`;
7. require `ok`;
8. require interrupt status `0`;
9. require IRQ line false;
10. require deterministic state hash changes exactly because modeled device state changed.

This test must fail on the current baseline because step 6 returns `unsupported_register`.

### 7.2 Bus-level RED

Extend `test_ps1_memory_bus.cpp` to reproduce the same bank-1 write through `Ps1MemoryBus` with diagnostic MMIO probing enabled.

Require:

- `write8(0x1F801803, 0x07)` returns `R3000aBusStatus::ok` after selecting bank 1;
- no `last_diagnostic_mmio_probe` is produced;
- no `last_unsupported_access` is produced for this write;
- the CD-ROM internal interrupt status becomes zero;
- the CD-ROM IRQ line deasserts;
- the already latched bus `I_STAT` CD-ROM bit remains set until acknowledged through existing I_STAT semantics.

This test proves the commercial frontier is crossed through production semantics, not diagnostic fallback.

### 7.3 Fail-closed negative RED/GREEN contracts

Add negative cases proving:

- bank-1 `0x20`, `0x40`, `0x80`, and any value containing those high bits remain unsupported;
- unsupported high-bit writes do not mutate the CD-ROM diagnostic state hash;
- unsupported high-bit writes do not clear interrupt status or IRQ state;
- existing bank-0 `0x1F801803 = 0` behavior remains unchanged;
- existing unsupported bank-0 non-zero writes remain unsupported.

## 8. Determinism and hashing

No new state field is required. Existing `interrupt_status_`, `response_`, `irq_line_`, and their current diagnostic hash coverage are sufficient.

Two identical HCLRCTL sequences from identical seed state must produce identical `diagnostic_state_hash()` values.

An unsupported high-bit HCLRCTL write must leave the state hash unchanged.

## 9. Error handling

The implementation must distinguish three cases:

- supported bank-0 request behavior already present;
- supported bank-1 HCLRCTL acknowledge with bits 0–4 only;
- unsupported bank-1 side-effect bits 5–7 or unsupported banks/register functions.

Unsupported cases remain explicit `unsupported_register` or the existing appropriate status. No diagnostic override is consumed for side-effectful writes.

## 10. Validation gates

C1 synthetic completion requires:

1. the new device-level RED fails on the baseline for the expected unsupported bank-1 write;
2. the new bus-level RED fails for the same reason;
3. the minimal implementation turns both GREEN;
4. all existing CD-ROM and memory-bus tests remain GREEN;
5. the complete Linux CI suite passes;
6. the complete Windows x64/MSVC suite passes;
7. the Windows artifact is produced from the exact final C1 SHA.

Synthetic GREEN proves only the modeled semantics. It does not close C1 commercially.

## 11. Commercial exit gate

After the final C1 artifact is produced, run `EXECUTAR CHECKPOINT` again against the same local legal JoJo `ps1_m1` installation.

C1 closes only if the strict commercial checkpoint demonstrates one of:

- more than 816,561 retired instructions before the next terminal blocker;
- a terminal strict frontier later than the `0x1F801803` bank-1 `0x07` write;
- a new strict Frame-First landmark.

The next checkpoint must not terminate on the same HCLRCTL write. If it does, C1 is not complete even if synthetic tests pass.

The new checkpoint becomes the evidence input for the next Phase C/D blocker batch.

## 12. Completion definition

C1 is complete only when:

- bank-1 HCLRCTL acknowledge bits 0–4 are implemented with fail-closed treatment of bits 5–7;
- the observed JoJo `0x07` acknowledge is handled strictly without diagnostic fallback;
- interrupt-controller and CD-ROM IRQ-state ownership remain separate;
- Linux and Windows CI are green;
- a new Windows artifact is produced;
- the refreshed commercial checkpoint crosses the exact strict blocker at 816,561 instructions and reaches a later frontier or first-frame landmark.

Anything less is implementation progress, not commercial C1 completion.