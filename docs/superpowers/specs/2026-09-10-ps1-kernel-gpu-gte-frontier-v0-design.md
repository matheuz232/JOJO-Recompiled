# PS1 Kernel BIOS + GP1 + GTE Transfer Frontier v0 — Design

Date: 2026-09-10
Baseline: `c83c20b823c6e54e0db6102c8f9fdd57329c6e99`
Design branch: `design/ps1-kernel-gpu-gte-v0`

## 1. Purpose

Advance the commercial JoJo PS1 boot path beyond the dependencies discovered by the latest MAX3 checkpoint without introducing speculative production semantics.

The checkpoint exposed, in order, the following new dependencies:

1. BIOS B0/18 `ResetEntryInt`
2. GPU GP1 MMIO at `0x1F801814`
3. BIOS B0/56 `GetC0Table`
4. BIOS A0/44 `FlushCache`
5. COP2/GTE control transfer `CTC2 $t0,$29` (`cnt29 = ZSF3`)

The best path reaches the CTC2 after SYS(02h) and ordinary kernel code. No frame, GP0 draw command, DMA transfer, or GTE math command has yet been observed on that path.

## 2. Non-goals

This milestone does not implement:

- Sony BIOS ROM bytes or any copyrighted BIOS payload;
- full exception/interrupt execution;
- event/thread kernel semantics;
- GP0 primitive parsing or rasterization;
- VRAM rendering/presentation;
- GPU DMA channel transfers;
- GTE data-register transfers (`MFC2`/`MTC2`);
- GTE mathematical commands (`RTPS`, `RTPT`, `NCLIP`, `AVSZ*`, etc.);
- timing-accurate GTE pipelines;
- CD-ROM/SPU/controller work.

Unsupported behavior remains an explicit boundary rather than a guessed success.

## 3. Sources and compatibility profile

Primary semantic source: PSX-SPX PlayStation Specifications.

For the small amount of guest-visible retail kernel layout needed by B0/18 and B0/56, use the common SCPH-1001/retail-kernel layout documented by public reverse-engineering references:

- C0 table base: `0x00000674`;
- B0 table base: `0x00000874` (reserved for future B0/57 support; not required by this milestone);
- exception handler entry used by C0[6]: `0x00000C80`;
- default EntryInt jmp_buf: `0x00006CF4`;
- kernel stack top value: `0x000085D8`, therefore default saved SP `0x000085D4`;
- ReturnFromException kernel routine address in the common retail layout: `0x00000F40`.

These are synthesized numeric layout values and data structures, not copied executable BIOS code. This milestone is explicitly a `retail_v25_compat` HLE profile; later profiles may vary guest-visible addresses without changing the dispatcher API.

## 4. Architecture

### 4.1 Ps1HleBios gains controlled guest-memory access

Change the dispatcher contract from CPU-only state mutation to:

```cpp
Ps1HleBiosResult dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu,
    Ps1MemoryBus& bus) noexcept;
```

`Ps1HleBios` remains the single owner of BIOS semantics. The bus is supplied only so HLE routines that are specified to expose guest-visible kernel structures can materialize them in RAM.

The runtime remains responsible for recognizing A0/B0/C0/SYS boundaries and for MAX3 fallback policy. It does not regain BIOS service semantics.

### 4.2 Ps1GpuState

Add a focused `Ps1GpuState` component owned by `Ps1MemoryBus`.

Responsibilities:

- accept documented GP1 control commands;
- maintain display/DMA/control state needed to derive GPUSTAT;
- expose counters and diagnostic state hash;
- reject unsupported GP1 commands explicitly;
- contain no rasterizer and no GP0 primitive parser.

`Ps1MemoryBus` maps `0x1F801814` to this component before the diagnostic MMIO shadow path.

### 4.3 R3000aCop2Gte control state

Extend `R3000aState` with a GTE/COP2 control-register bank:

```cpp
struct R3000aCop2Gte {
    std::array<std::uint32_t, 32> control{};
};
```

This milestone implements only CTC2/CFC2 access to those 32 control registers. Data registers and GTE commands remain explicit COP2 boundaries.

This keeps GTE register state part of the architectural CPU snapshot, so MAX3 copies and fingerprints it naturally.

## 5. BIOS semantics

### 5.1 A0/44 FlushCache

`FlushCache()` is a void BIOS routine that invalidates the PS1 instruction cache so subsequent opcodes are fetched from RAM.

The current JOJO runtime is a reference interpreter and does not maintain a translated or instruction cache. Therefore A0/44 is a deterministic no-op with respect to memory and CPU data state:

- preserve `$v0`;
- return through `$ra` using normal A0/B0/C0 HLE return semantics;
- increment BIOS-call reporting as usual;
- do not alter DMA, RAM, or unrelated registers.

When a native translated-code cache exists, cache invalidation must be added behind this same HLE entry.

### 5.2 B0/18 ResetEntryInt

Implement the common retail default Exit/EntryInt structure in guest RAM at `0x00006CF4`.

Materialized 0x30-byte jmp_buf layout:

- `+0x00`: `0x00000F40` (common retail `ReturnFromException` entry);
- `+0x04`: `0x000085D4` (kernel stacktop minus 4);
- `+0x08`: `0` (FP);
- `+0x0C..+0x28`: `0` (S0..S7);
- `+0x2C`: `0` (GP).

Behavior:

- materialize/refresh this structure every time B0/18 is called;
- set the HLE `interrupt_hook_address_` to `0x00006CF4`;
- return `0x00006CF4` in `$v0`;
- return through `$ra`.

This does not implement interrupt dispatch or B0/17 ReturnFromException execution. If later guest execution jumps to an unimplemented kernel routine, the normal CPU/frontier mechanisms remain authoritative.

### 5.3 B0/56 GetC0Table

Return the common retail C0 jump-list base `0x00000674` in `$v0`.

Before returning, ensure the minimal guest-visible table is materialized in RAM:

- 0x1E 32-bit entries (0x78 bytes) are allocated/zero-initialized in the normal 2 MiB RAM image;
- entry 6 (`0x674 + 6*4`) is initialized to `0x00000C80`, matching the common retail exception-handler entry used by known game patching code;
- existing guest writes to the table are preserved on subsequent `GetC0Table` calls after first initialization; do not overwrite patches every time.

The HLE object therefore needs a boolean `c0_table_materialized_` state included in its diagnostic hash.

This milestone does not make arbitrary patched C0 table entries alter HLE dispatch. The returned table exists for code that inspects/patches the common exception handler. If the game later depends on dynamic table redirection, that becomes a separately evidenced milestone rather than being silently guessed here.

## 6. GPU GP1 semantics

### 6.1 MMIO mapping

`0x1F801814` write32 is the GP1 command port. The upper eight bits select the command and the low 24 bits are parameters.

`0x1F801814` read32 returns a derived GPUSTAT value.

The address is handled as real MMIO and must never produce a speculative-MMIO event for commands this component supports.

### 6.2 Supported commands

Implement the documented initialization/control subset needed to move commercial boot forward:

- GP1(00h) Reset GPU
- GP1(01h) Reset Command Buffer
- GP1(02h) Acknowledge GPU IRQ
- GP1(03h) Display Enable/Disable
- GP1(04h) DMA Direction
- GP1(05h) Display VRAM Start
- GP1(06h) Horizontal Display Range
- GP1(07h) Vertical Display Range
- GP1(08h) Display Mode

Any other GP1 command remains unsupported in production and returns an MMIO boundary. The diagnostic shadow must not absorb an unsupported command merely because the address itself is known.

### 6.3 Reset/default state

GP1(00h) establishes the documented reset state, including:

- display disabled;
- DMA direction off;
- VRAM display start = 0;
- horizontal range start `0x200`, end `0x200 + 256*10`;
- vertical range start `0x010`, end `0x010 + 240`;
- display mode command bits = 0 (320x200 NTSC description used by PSX-SPX reset table);
- GPU IRQ flag cleared;
- command buffer reset state/counter advanced.

The derived GPUSTAT reset value must be `0x14802000` before later supported GP1 commands alter reflected fields.

### 6.4 State and counters

`Ps1GpuState` stores at least:

- `display_disabled`;
- `dma_direction` (2 bits);
- `display_vram_x`, `display_vram_y`;
- horizontal start/end;
- vertical start/end;
- display-mode low command bits;
- `irq1` flag;
- `gp1_command_count`;
- `command_buffer_reset_count`.

`Ps1MemoryBus::diagnostic_state_hash()` incorporates all state above.

`Ps1BootReport::gpu_gp1_command_count` must report real GP1 command deltas so MAX3 ranking can prefer paths that reach more real hardware work.

No presented frame or VRAM write is synthesized by GP1 control commands.

## 7. GTE CTC2/CFC2 semantics

### 7.1 CU2 gate

Preserve existing behavior:

- if COP0 Status.CU2 is clear, CTC2/CFC2 raise Coprocessor Unusable for coprocessor 2;
- only when CU2 is set may the transfer execute.

### 7.2 CTC2

CTC2 writes a GPR value into GTE control register `rd` with hardware-visible normalization.

Register classes:

1. Packed matrix-pair registers: cnt0-3, cnt8-11, cnt16-19 store all 32 bits (two signed 16-bit fields).
2. Matrix tail registers cnt4, cnt12, cnt20 store only the low signed 16-bit element in canonical sign-extended 32-bit form.
3. Full 32-bit registers cnt5-7, cnt13-15, cnt21-25, cnt28 store all 32 bits.
4. cnt26 `H` stores low 16 bits; its CFC2 read reproduces the documented sign-extension bug.
5. cnt27 `DQA`, cnt29 `ZSF3`, cnt30 `ZSF4` store low signed 16 bits in canonical sign-extended form.
6. cnt31 `FLAG`: writable bits are 30..12; bits 11..0 read as zero; bit31 is derived from `(bits30..23 OR bits18..13) != 0` and is not directly writable.

The observed commercial instruction `CTC2 $t0,$29` with `$t0=0x155` therefore stores canonical `ZSF3=0x00000155` and advances normally.

CTC2 has no GPR result and is not a GPR delayed load.

### 7.3 CFC2

CFC2 schedules a one-instruction delayed GPR load, matching the documented GTE transfer delay.

Read normalization:

- packed matrix-pair and full 32-bit classes return stored 32-bit values;
- cnt4/cnt12/cnt20 return sign-extended low 16 bits;
- cnt26 H also returns sign-extended low 16 bits (documented hardware bug even though H is unsigned for calculations);
- cnt27/cnt29/cnt30 return sign-extended low 16 bits;
- cnt31 returns writable flags plus derived bit31 with bits11..0 zero.

If a direct-write instruction targets the same GPR in the immediately following instruction, existing R3000A delayed-load arbitration rules remain authoritative.

### 7.4 Still unsupported

After this milestone:

- MFC2/MTC2 data-register transfers remain `cop2_unimplemented`;
- COP2 immediate math commands remain `cop2_unimplemented`;
- LWC2/SWC2 remain unchanged/unsupported unless already separately modeled.

This is intentional. The next checkpoint should tell us which GTE data transfer or command is actually required next.

## 8. MAX3 integration

MAX3 behavior is unchanged in shape. New real state must affect deduplication:

- HLE kernel materialization flags/state are already part of `Ps1HleBios::diagnostic_state_hash()` and must be extended for this milestone;
- GP1 state must be included through `Ps1MemoryBus::diagnostic_state_hash()`;
- GTE control registers must be included in `Ps1BootRuntime::diagnostic_state_hash()` as part of CPU state hashing.

The current report format remains `jojo-max3-checkpoint-v1` unless a new field is required. No format bump is needed solely for these state additions.

## 9. Error and boundary policy

- Unknown BIOS selector: `bios_call_unimplemented` as today, branchable by MAX3.
- Unsupported GP1 command at the known GP1 address: real MMIO boundary; no speculative shadow fallback for the known command port.
- CU2 disabled: architectural Coprocessor Unusable exception.
- MFC2/MTC2 or GTE command after CU2 enabled: `cop2_unimplemented` boundary.
- Failed HLE RAM materialization (unexpected bus failure): HLE must return an explicit terminal/failure disposition; do not return success with a partial structure.

## 10. TDD requirements

### Kernel BIOS tests

RED then GREEN tests must cover:

- A0/44 preserves `$v0`, returns to `$ra`, does not mutate RAM;
- B0/18 materializes exact 0x30-byte default jmp_buf, stores hook pointer, returns address;
- a second B0/18 refreshes the default structure after guest mutation;
- B0/56 returns `0x674` and seeds C0[6] = `0xC80`;
- a second B0/56 preserves a guest patch to C0[6];
- unrelated BIOS selectors remain unsupported.

### GP1 tests

RED then GREEN tests must cover:

- GP1 writes bypass diagnostic shadow;
- reset GPUSTAT = `0x14802000`;
- commands 03/04/05/06/07/08 update the correct state/status fields;
- command count increments;
- unsupported GP1 command remains a boundary;
- no GP1 command increments presented-frame or VRAM-write counters.

### GTE tests

RED then GREEN tests must cover:

- CTC2 with CU2 clear raises Coprocessor Unusable;
- observed `CTC2 rt,cnt29` stores `0x155` and advances;
- signed 16-bit canonicalization for cnt29/cnt30/cnt27 and matrix tails;
- CFC2 has exactly one-instruction GPR load delay;
- H cnt26 read sign-extension bug;
- FLAG writable mask and derived bit31;
- MFC2/MTC2 and GTE command remain boundary after CU2 enable.

### Commercial regression test

Add a synthetic sequence matching the final observed trace shape:

- enable CU2 via COP0 Status;
- load `$t0 = 0x155`;
- execute `CTC2 $t0,$29`;
- verify execution proceeds beyond the former boundary.

## 11. CI and completion gate

Completion requires fresh evidence on the exact final SHA:

1. Linux configure/build/readiness/PS1 architecture/ctest all green.
2. Windows x64 MSVC 2022 Release build and all tests green.
3. Existing observed-disc and UDP contract gates green.
4. Windows artifact uploaded.
5. Artifact downloaded, local ZIP SHA-256 matches GitHub digest, and ZIP contains `JOJO-Recompiled.exe`.
6. Diff audit confirms no proprietary BIOS/game data was added.

Only after those gates is the next MAX3 executable delivered to the user.

## 12. Expected next evidence

If this design behaves as intended, the next commercial checkpoint should pass:

- B0/18 without speculation;
- the observed supported GP1 initialization command(s) without MMIO shadow;
- B0/56 and A0/44 without BIOS fallback;
- CTC2 cnt29 without COP2 boundary.

The likely next stop is one of:

- another stateful BIOS kernel call;
- GP0/GPU DMA work;
- MFC2/MTC2 data-register access;
- the first actual GTE math command.

That next stop, rather than speculation, determines the following milestone.
