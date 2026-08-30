# PS1 R2 — R3000A Foundation Implementation Plan

> **Execution:** implement inline with strict RED → GREEN → refactor cycles. No commercial-game progress claim may be inferred from unit-test coverage alone.

## Goal

Create a correctness-first PlayStation R3000A-compatible reference CPU foundation that can eventually execute the supported `SLUS_010.60` payload. R2 begins with a reference interpreter and architectural state; native x64 recompilation comes only after the reference behavior is trustworthy.

## Non-negotiable behavior

- GPR0 is hard-wired to zero.
- Branches and jumps execute exactly one delay-slot instruction before transferring control.
- JAL/JALR link to the instruction after the delay slot (`PC + 8`).
- Loads have the R3000A one-instruction load delay; the instruction immediately following a load observes the old target-register value.
- Signed ADD/ADDI/SUB overflow raises an architectural exception rather than wrapping; unsigned variants wrap.
- Misaligned halfword/word accesses become address-error exceptions.
- Unknown/reserved opcodes become explicit diagnostics/exceptions; never silent NOPs.
- COP0 exception state and branch-delay exception metadata must be modeled before commercial boot claims.
- No proprietary game bytes in permanent tests.

## Task 1 — CPU state, integer ALU and control-flow pipeline

**Create:** `src/core/psx_r3000a.h`
**Test:** extend current PS1 intake test target temporarily, then split to `tests/test_psx_r3000a.cpp` when the PS1 target boundary is refactored.

RED assertions:
- reset sets `pc=entry`, `next_pc=entry+4`, all GPRs zero;
- writes to r0 are discarded;
- ADDU and SUBU update destination registers with 32-bit wrapping semantics;
- BEQ taken leaves `pc` at the delay-slot instruction and sets `next_pc` to the branch target;
- after stepping the delay-slot NOP, `pc` becomes the branch target;
- JAL sets r31 to `branch_pc + 8` while still executing the delay slot first;
- unsupported opcode is reported explicitly.

GREEN implementation:
- state with 32 GPRs, HI/LO, `pc`, `next_pc` and minimal stop/exception result;
- instruction field extraction;
- state advance model where current `pc` advances to old `next_pc`, sequential `next_pc += 4`, then a branch/jump overrides only `next_pc`;
- minimal opcodes required by the RED test only.

## Task 2 — Complete base integer/logical instruction slice

RED coverage:
- ADD/ADDI/SUB signed overflow;
- ADDIU, AND/ANDI, OR/ORI, XOR/XORI, NOR;
- SLT/SLTI signed and SLTU/SLTIU unsigned;
- LUI;
- SLL/SRL/SRA and variable shifts;
- BNE/BLEZ/BGTZ and REGIMM BLTZ/BGEZ plus link variants;
- J/JR/JALR.

GREEN:
- implement only documented MIPS-I/R3000A semantics;
- preserve branch-delay behavior for every control-flow instruction.

## Task 3 — HI/LO multiply/divide

RED coverage:
- MULT/MULTU 64-bit results;
- DIV/DIVU quotient/remainder;
- divide-by-zero edge behavior;
- signed `INT32_MIN / -1` edge behavior;
- MFHI/MFLO/MTHI/MTLO.

GREEN:
- exact R3000A/PS1-visible HI/LO behavior, with no host-language undefined behavior.

## Task 4 — PS1 RAM bus, loads/stores and load-delay pipeline

**Create:** `src/core/psx_memory.h` and implementation boundary.

RED coverage:
- KUSEG/KSEG0/KSEG1 mirrors of main RAM where appropriate;
- LB/LBU/LH/LHU/LW and SB/SH/SW;
- halfword/word alignment exceptions;
- one-instruction load delay including overwrite/cancellation cases;
- LWL/LWR/SWL/SWR added only with exact hardware semantics.

GREEN:
- minimal main-RAM/scratchpad bus first;
- unknown MMIO accesses return structured diagnostics instead of fake values.

## Task 5 — COP0 exceptions and interrupts

RED coverage:
- Reserved Instruction, Overflow, Address Error Load/Store, Syscall and Break;
- EPC, Cause.Excode, Cause.BD and Status mode/interrupt stack behavior;
- MFC0/MTC0 load-delay-visible behavior as required by PS1;
- RFE;
- interrupt eligibility from Status/Cause masks.

GREEN:
- exception vector/state transition consistent with PS1 R3000A behavior;
- branch-delay exceptions save the branch PC and set BD.

## Task 6 — PS-X EXE loader and first commercial execution evidence

RED/synthetic first:
- copy PS-X EXE payload into PS1 RAM at header load address;
- initialize PC/GP/SP from parsed header/SYSTEM.CNF policy;
- reject payload/range conflicts.

Then real-media development probe:
- use the user's supplied `SLUS_010.60` bytes locally only;
- execute from real `0x8001000c` using the reference CPU;
- record the first unsupported instruction/MMIO/exception reached with PC/opcode context;
- extend the runtime one real requirement at a time.

R2 is **not complete** when a handful of instructions execute. Commercial boot progression must be demonstrated through real game execution, and later GPU/CD/SPU/GTE/device work remains separate gates.

## Verification policy

Every behavioral slice requires:
1. permanent synthetic RED test;
2. observed expected RED in CI;
3. minimal implementation;
4. Linux + Windows GREEN at exact head;
5. no new warnings in new PS1 code;
6. explicit statement of what the evidence does not yet prove.
