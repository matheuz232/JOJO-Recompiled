# JOJO Recompiled — R3000A Reference Core M2 Design

Date: 2026-09-09
Status: design approved in chat; implementation not started
Base: `main` after PS1 M0+M1 integration
Scope: JoJo PS1 only

## 1. Goal

M2 establishes a deterministic R3000A/MIPS-I reference CPU core that becomes the semantic oracle for later JoJo-specific PS1 analysis, IR lowering and Windows x64 native code generation.

M2 does **not** implement the PS1 GPU, SPU, CD-ROM, DMA engine, timers, controller hardware, complete BIOS HLE, game rendering or gameplay. It also does not claim that the commercial game boots.

The reference core exists to answer one question precisely: given an R3000A CPU state, an instruction word and a bus result, what architectural state must exist after the instruction, including delay-slot, load-delay and exception behavior?

## 2. Relationship to M1

M1 already provides the PS1-only media/conversion path:

`BIN/CUE/ISO -> ISO9660 -> SYSTEM.CNF -> PS-X EXE -> manifest v2 -> active installation generation`.

M2 consumes only verified M1 outputs. It must not reopen Dreamcast/SH-4 paths or reinterpret a v1 legacy installation as PS1-compatible.

For an installed PS-X EXE, M2 may initialize a CPU state from verified executable metadata, but executing real commercial code beyond the semantic contracts is not evidence of boot or gameplay.

## 3. Architectural choice

Use a semantic-first reference executor before CFG/IR/x64 work.

The active dependency direction is:

```text
instruction bytes / words
        |
        v
  mips_decoder
        |
        v
r3000a_reference_executor
   |        |        |
   |        |        +--> structured diagnostics
   |        +-----------> R3000A architectural state
   +--------------------> abstract R3000A bus
```

The CPU core never dereferences guest addresses as host pointers. All memory traffic crosses the bus interface.

## 4. Modules and ownership

M2 introduces focused modules with narrow interfaces:

- `src/core/mips_decoder.h/.cpp`
  - decode one 32-bit little-endian MIPS instruction word into a typed instruction representation;
  - preserve the original raw word;
  - expose decoded register/immediate/target fields;
  - never guess an unknown encoding as a supported instruction.

- `src/core/r3000a_state.h`
  - architectural CPU state only;
  - 32 GPRs, HI, LO, PC/next-PC, pending delayed load, COP0 state and exception bookkeeping;
  - no host window/audio/input state.

- `src/core/r3000a_bus.h`
  - abstract guest memory operations;
  - `read8/read16/read32` and `write8/write16/write32` with explicit failure results;
  - future PS1 bus implementation plugs into this interface.

- `src/core/r3000a_diagnostics.h`
  - stable diagnostic categories and context fields;
  - at minimum guest PC, raw opcode, access address/width/value when applicable, exception code and stage.

- `src/core/r3000a_reference_executor.h/.cpp`
  - one-instruction semantic execution;
  - delay/load pipelines;
  - exception entry;
  - COP0 subset;
  - deterministic interaction with `R3000aBus`.

Tests remain synthetic and contain no commercial game bytes.

## 5. CPU state

The minimum state is conceptually:

```text
GPR[32]
HI
LO
PC
next_PC
pending_load { valid, register_index, value }
COP0.Status
COP0.Cause
COP0.EPC
COP0.BadVAddr
exception/branch-delay bookkeeping
```

`GPR[0]` is architecturally hard-wired to zero. Any attempted write to register 0 is discarded. The invariant is reasserted at the public executor boundary so a malformed test fixture cannot leave `$zero` non-zero after a step.

The executor uses 32-bit guest arithmetic. Host integer width must not leak into guest overflow, shift, sign-extension or multiplication behavior.

## 6. Program-counter model and delay slots

The executor models `PC` and `next_PC` explicitly.

For an ordinary instruction:

```text
executed instruction address = PC
PC      <- next_PC
next_PC <- next_PC + 4
```

A taken branch or jump changes the future `next_PC`; the instruction already selected at the sequential `PC + 4` executes first as the architectural delay slot.

A not-taken conditional branch still executes its sequential delay-slot instruction.

Direct branch targets use sign-extended immediate displacement shifted left by two and added to `PC + 4` of the branch instruction. `J`/`JAL` targets combine the high four bits of `PC + 4` with the 26-bit target shifted left by two.

`JAL` and link variants write the architectural return address corresponding to the instruction after the delay slot (`branch_pc + 8`). `JALR` follows the same rule for its selected destination register.

Control-transfer behavior inside a delay slot is treated according to the R3000A contract selected by the implementation tests; unsupported/unpredictable combinations must not be silently normalized into a different ISA model.

## 7. Delayed-load model

M2 models the R3000A one-instruction load delay explicitly.

For:

```asm
LW   $t0, 0($a0)
ADDU $t1, $t0, $zero
ADDU $t2, $t0, $zero
```

the first `ADDU` observes the old `$t0`; the second observes the loaded value.

Execution ordering must make this behavior testable without ad-hoc instruction-specific exceptions:

1. read source operands for the current instruction from the pre-step architectural register view;
2. calculate the current instruction result;
3. retire the previously pending delayed load;
4. apply the current instruction's direct GPR write, if any;
5. schedule a new pending load produced by the current instruction;
6. reassert `$zero == 0`.

If the instruction immediately after a load writes the same GPR directly, that newer architectural write wins over the older delayed load. Tests lock this rule.

Loads into `$zero` perform their memory access and fault behavior normally but do not create visible register state.

## 8. Integer instruction baseline

M2 implements the R3000A integer instructions required as the semantic foundation for JoJo analysis. The initial baseline is:

### Shifts and register ALU

- `SLL`, `SRL`, `SRA`;
- `SLLV`, `SRLV`, `SRAV`;
- `ADD`, `ADDU`, `SUB`, `SUBU`;
- `AND`, `OR`, `XOR`, `NOR`;
- `SLT`, `SLTU`.

### Immediate ALU

- `ADDI`, `ADDIU`;
- `ANDI`, `ORI`, `XORI`;
- `LUI`;
- `SLTI`, `SLTIU`.

### HI/LO

- `MFHI`, `MTHI`, `MFLO`, `MTLO`;
- `MULT`, `MULTU`, `DIV`, `DIVU`.

Multiplication produces the architecturally correct 64-bit product split across HI/LO.

Division is deterministic and tested for normal operands, divide-by-zero and signed `INT32_MIN / -1` behavior according to the R3000A contract. Host-language undefined behavior is forbidden.

### Branch/jump

- `J`, `JAL`, `JR`, `JALR`;
- `BEQ`, `BNE`, `BLEZ`, `BGTZ`;
- `BLTZ`, `BGEZ`, `BLTZAL`, `BGEZAL`.

### Loads/stores

- `LB`, `LBU`, `LH`, `LHU`, `LW`;
- `SB`, `SH`, `SW`;
- `LWL`, `LWR`, `SWL`, `SWR`.

The unaligned merge instructions are included in M2 because they are architectural R3000A memory operations and are common enough that deferring them would weaken the reference oracle.

### System/control

- `SYSCALL`;
- `BREAK`;
- COP0 subset described below;
- COP2/GTE decode boundary described below.

Any encoding outside the supported contract produces an explicit unsupported-instruction result containing at least `pc` and `opcode`.

## 9. Arithmetic overflow

`ADD`, `ADDI` and `SUB` trap on signed two's-complement overflow.

`ADDU`, `ADDIU` and `SUBU` wrap modulo 2^32 and never raise signed-overflow exceptions.

Overflow detection is implemented without relying on host signed-overflow behavior.

When a trapping arithmetic instruction faults, its destination register is not modified and architectural exception entry occurs.

## 10. Memory access and alignment

The CPU core is little-endian and delegates actual address mapping to `R3000aBus`.

Alignment rules:

- `LH/LHU/SH` require 2-byte alignment;
- `LW/SW` require 4-byte alignment;
- byte accesses require no extra alignment;
- `LWL/LWR/SWL/SWR` implement their architectural byte-lane merge semantics and therefore are not rejected merely because the effective address is not word-aligned.

Misaligned loads/instruction fetches raise address-error-load/fetch (`AdEL`) as appropriate. Misaligned stores raise address-error-store (`AdES`). `BadVAddr` records the faulting guest address.

A bus failure that represents an unsupported physical/MMIO region remains distinguishable from an architectural alignment exception. M2 does not fabricate zero reads for unknown hardware.

## 11. Exceptions and delay-slot precision

M2 models exception entry accurately enough to become the later runtime oracle.

At minimum it supports exception causes required by this milestone:

- interrupt entry hook;
- address error load/fetch;
- address error store;
- syscall;
- breakpoint;
- reserved/unsupported instruction when mapped to architectural RI behavior;
- arithmetic overflow;
- coprocessor unusable where architecturally applicable.

On exception:

- `Cause.ExcCode` reflects the exception type;
- `EPC` identifies the correct restart location;
- if the faulting instruction is in a branch delay slot, `Cause.BD` is set and `EPC` refers to the branch instruction, not the delay-slot instruction;
- `BadVAddr` is updated for address exceptions;
- current interrupt/user mode state is pushed using the R3000A COP0 status stack semantics;
- control transfers to the appropriate general exception vector selected by `Status.BEV`.

The reference executor must track enough branch-origin metadata to derive `BD` and `EPC` deterministically.

## 12. COP0 subset

M2 does not emulate every implementation-specific COP0 register. It implements the subset needed for architectural exceptions and the next PS1 runtime milestone:

- `Status`;
- `Cause`;
- `EPC`;
- `BadVAddr`;
- `MFC0` for supported registers;
- `MTC0` with register-specific writable-bit masking;
- `RFE` using R3000A status-stack restore semantics.

Unsupported COP0 register access is diagnostic, not silently accepted.

An external interrupt-pending input can be injected into the reference core for tests. Device generation of those interrupt lines belongs to the later PS1 bus/device milestone.

## 13. COP2/GTE boundary

M2 recognizes COP2/GTE instruction classes sufficiently to report them distinctly from unknown MIPS instructions.

It does **not** implement GTE arithmetic in this milestone.

The executor returns a structured `cop2_unimplemented`/equivalent boundary result containing `pc` and `opcode`, unless the architectural state requires a coprocessor-unusable exception first. This boundary lets later JoJo execution evidence identify exactly which GTE operations must be implemented.

No COP2 instruction is treated as a NOP.

## 14. Decoder contract

The decoder is pure and side-effect free.

Given one 32-bit word, it returns:

- raw word;
- opcode class;
- `rs`, `rt`, `rd`, shift amount where applicable;
- immediate/target fields in raw form;
- typed operation identifier.

Sign extension, branch target calculation and architectural effects belong to execution helpers rather than being hidden inside the parser.

Decoder tests cover valid encodings and representative reserved encodings.

## 15. Executor API behavior

The reference executor exposes a deterministic one-step API conceptually equivalent to:

```cpp
Result<R3000aStepResult> step_r3000a(
    R3000aState& state,
    R3000aBus& bus);
```

The actual signature may use the project's existing `Result<T>` and diagnostic conventions, but the semantic boundary is fixed:

- fetch one instruction through the bus at `state.pc`;
- execute exactly one architectural instruction;
- update state or enter an architectural exception;
- report non-architectural unsupported boundaries explicitly.

A separate helper may execute an already-decoded instruction for unit testing, but production stepping must exercise instruction fetch through the bus so fetch alignment/fault behavior is covered.

## 16. PS-X EXE initialization

M2 adds a pure initialization helper that consumes verified M1 executable metadata plus a caller-provided prepared bus/memory image.

Initialization sets at least:

- `PC = psx_exe_entry`;
- `next_PC = psx_exe_entry + 4`;
- `GP = psx_exe_initial_gp`;
- initial stack register from the verified PS-X EXE stack fields when the image specifies them;
- `$zero = 0`;
- HI/LO and pending-load state to deterministic reset values defined by the helper contract.

M2 does not claim BIOS reset-state emulation. Full reset/BIOS-HLE startup semantics are a later milestone. This helper represents entry into the verified program image under the JoJo-specific BIOS-free runtime architecture.

## 17. Diagnostics

Unsupported or failed operations expose structured context rather than prose-only errors.

At minimum, diagnostics can carry:

```text
error_code
stage
pc
opcode
address
access_width
access_type
write_value
exception_code
coprocessor
register_index
```

Fields not relevant to an error are absent/unset rather than filled with fabricated values.

Diagnostic formatting for user logs is separate from the structured semantic result.

## 18. Testing strategy

M2 is developed TDD RED -> GREEN in small instruction families.

Required test groups:

1. decoder field extraction and reserved encodings;
2. `$zero`, register ALU and immediates;
3. overflow/no-overflow pairs;
4. HI/LO multiply/divide edge cases;
5. branches, jumps and link values;
6. branch delay slots;
7. delayed loads, including same-destination overwrite behavior;
8. aligned loads/stores and sign extension;
9. `LWL/LWR/SWL/SWR` merge tables for all low address bits;
10. address exceptions and `BadVAddr`;
11. syscall/break/reserved instruction exception entry;
12. exception in a branch delay slot, including `Cause.BD` and `EPC`;
13. COP0 `MFC0/MTC0/RFE` masking/state-stack behavior;
14. interrupt-pending entry using a synthetic injected line;
15. COP2/GTE boundary diagnostics;
16. PS-X EXE CPU-state initialization;
17. deterministic replay: identical initial state + bus contents -> identical final state/diagnostics.

Tests use synthetic instruction words and synthetic RAM/bus fixtures only.

Linux and Windows/MSVC CI are both required before M2 is considered complete.

## 19. Readiness and truth boundary

M2 may promote the semantic status `reference-execution-ready` only when:

- the complete M2 synthetic semantic suite is green on Linux and Windows;
- PS-X EXE initialization is validated against synthetic M1 metadata;
- the executor can initialize the supported installed executable state without claiming unsupported hardware behavior as implemented;
- unsupported CPU/COP0/COP2 boundaries remain explicit and observable.

M2 does **not** promote:

- `native-codegen-ready`;
- `hardware-runtime-partial`;
- `boot-reached`;
- `rendering-verified`;
- `audio-verified`;
- `input-verified`;
- `gameplay-verified`.

A synthetic test can prove CPU semantics, not commercial-game compatibility.

## 20. Out of scope

Explicitly deferred:

- PS1 RAM mirrors/scratchpad/MMIO implementation beyond synthetic bus fixtures;
- BIOS A0/B0/C0 HLE services;
- DMA/timers/interrupt-controller devices;
- GPU/GTE arithmetic implementation;
- SPU/audio;
- CD-ROM command/streaming model;
- controller/SIO;
- MIPS CFG discovery;
- PS1 IR;
- Windows x64 code generation/cache;
- self-modifying-code/cache-coherency policy;
- commercial boot/render/audio/input/gameplay evidence.

These belong to later milestones and consume the M2 reference executor as their semantic authority.

## 21. Implementation sequencing constraint

The implementation plan must preserve dependency order:

```text
decoder/state/bus contracts
-> integer ALU
-> HI/LO
-> branches/jumps
-> delay-slot machinery
-> memory + delayed loads
-> exceptions
-> COP0/RFE/interrupt hook
-> COP2 boundary
-> PS-X EXE initialization
-> full Linux + Windows verification
```

Individual tasks may be split more finely for TDD, but later layers may not be used to bypass missing earlier semantics.

## 22. Completion definition

M2 is complete only when all planned semantic contracts are implemented, the full project CI is green on Linux and Windows, the active PS1 architecture gate remains green, and documentation records the exact implemented CPU scope without claiming game boot.

The next design cycle after M2 is the JoJo-specific PS1 memory/bus and BIOS/HLE foundation. CFG/IR/native x64 work must use this reference core as the oracle rather than creating an independent second interpretation of R3000A semantics.
