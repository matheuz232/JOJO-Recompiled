# R3000A Reference Core M2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic PS1 R3000A/MIPS-I reference executor that is the semantic oracle for later JoJo CFG/IR/x64 work, without claiming commercial boot or implementing PS1 devices.

**Architecture:** Decode one 32-bit MIPS word into a typed instruction, execute through a stateful one-step R3000A reference core, and route every guest memory access through an abstract bus. Delay slots, delayed GPR loads, precise exceptions, COP0 state, interrupt admission and COP2/GTE boundaries are explicit state transitions rather than hidden host behavior.

**Tech Stack:** C++20, CMake 3.20+, CTest, MSVC 2022 x64, GCC/Clang-compatible portable core, existing `jojo_core` static library and project-local test executables.

**Spec:** `docs/superpowers/specs/2026-09-09-r3000a-reference-core-m2-design.md`

## Global Constraints

- Active guest platform remains Sony PlayStation 1 only; never reintroduce Dreamcast/SH-4 guest code.
- Scope remains JoJo PS1 only; this milestone is not a general PlayStation emulator.
- No game image, PS-X EXE, BIOS, extracted asset or other commercial byte may enter source, tests, CI or artifacts.
- R3000A guest arithmetic and addresses are exactly 32-bit; host signed-overflow and host pointer aliasing may not define guest semantics.
- All guest memory traffic crosses `R3000aBus`; executor code never treats a guest address as a host pointer.
- `$zero` is hard-wired to zero at the public step boundary.
- Reserved encodings raise architectural RI; recognized but deliberately unimplemented architectural functionality returns a structured implementation boundary.
- A control transfer encountered in an already active delay slot returns `unpredictable_delay_slot_control_transfer`; do not invent nested-delay-slot semantics.
- COP2/GTE arithmetic remains out of scope. Disabled COP2 raises CpU; enabled but unimplemented COP2 returns an explicit `cop2_unimplemented` boundary.
- Synthetic tests may prove CPU semantics only. They may not promote `boot-reached`, rendering, audio, input or gameplay claims.
- Linux and Windows/MSVC CI must both be green before M2 completion.

## File Structure

Create:

- `src/core/mips_decoder.h` — typed MIPS-I operation and decoded field contract.
- `src/core/mips_decoder.cpp` — pure decoder; no CPU state changes.
- `src/core/r3000a_state.h` — GPR/HI/LO/PC, delayed-load, delay-slot and COP0 state.
- `src/core/r3000a_bus.h` — abstract read/write bus result and interface.
- `src/core/r3000a_diagnostics.h` — exception/boundary/status enums and structured context.
- `src/core/r3000a_reference_executor.h` — public stepping and PS-X EXE initialization API.
- `src/core/r3000a_reference_executor.cpp` — semantic executor and exception helpers.
- `tests/mips_test_encode.h` — synthetic instruction encoders used by CPU tests.
- `tests/r3000a_test_bus.h` — deterministic synthetic bus fixture.
- `tests/test_mips_decoder.cpp` — decoder contract.
- `tests/test_r3000a_integer.cpp` — basic stepping, `$zero`, ALU and immediates.
- `tests/test_r3000a_exceptions.cpp` — exception entry, trapping ALU, RI, syscall/break and bus faults.
- `tests/test_r3000a_hilo.cpp` — multiply/divide and HI/LO.
- `tests/test_r3000a_control_flow.cpp` — branches/jumps, link semantics and delay slots.
- `tests/test_r3000a_memory.cpp` — aligned memory operations and one-instruction load delay.
- `tests/test_r3000a_unaligned.cpp` — LWL/LWR/SWL/SWR byte-lane tables and forwarding.
- `tests/test_r3000a_cop0.cpp` — COP0, RFE and interrupt admission.
- `tests/test_r3000a_boundary.cpp` — COP2 boundary and unsupported-address diagnostics.
- `tests/test_r3000a_init.cpp` — PS-X EXE state initialization and deterministic replay.

Modify:

- `CMakeLists.txt` — compile new core sources and register each test target.
- `PROJECT-STATE.md` — record exact M2 semantic scope only after full CI is green.
- `docs/NEXT-MILESTONES.md` — move next priority to PS1 memory/bus + BIOS/HLE after M2 verification.
- `docs/architecture/PRODUCTION-READINESS.tsv` — record M2/reference-core evidence without claiming boot/gameplay.

---

### Task 1: Pure MIPS-I Decoder

**Files:**
- Create: `src/core/mips_decoder.h`
- Create: `src/core/mips_decoder.cpp`
- Create: `tests/test_mips_decoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: one raw `std::uint32_t` instruction word.
- Produces: `jojo::MipsInstruction decode_mips(std::uint32_t raw) noexcept` and stable `MipsOp` identifiers used by every later task.

- [ ] **Step 1: Write the failing decoder test**

Create `tests/test_mips_decoder.cpp` with representative R/I/J, REGIMM, COP0 and COP2 cases plus a reserved encoding:

```cpp
#include "core/mips_decoder.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    const auto addu = jojo::decode_mips(0x012A4021u); // addu $t0,$t1,$t2
    CHECK(addu.op == jojo::MipsOp::addu);
    CHECK(addu.rs == 9u && addu.rt == 10u && addu.rd == 8u);

    const auto addiu = jojo::decode_mips(0x2528FFF0u);
    CHECK(addiu.op == jojo::MipsOp::addiu);
    CHECK(addiu.immediate == 0xFFF0u);

    const auto jal = jojo::decode_mips(0x0C004000u);
    CHECK(jal.op == jojo::MipsOp::jal);
    CHECK(jal.target == 0x004000u);

    CHECK(jojo::decode_mips(0x04100000u).op == jojo::MipsOp::bltzal);
    CHECK(jojo::decode_mips(0x42000010u).op == jojo::MipsOp::rfe);
    CHECK(jojo::decode_mips(0x48000000u).op == jojo::MipsOp::mfc2);
    CHECK(jojo::decode_mips(0x4A000000u).op == jojo::MipsOp::cop2_command);
    CHECK(jojo::decode_mips(0x70000000u).op == jojo::MipsOp::reserved);
    return failures ? 1 : 0;
}
```

Register only this test and the decoder source in `CMakeLists.txt`:

```cmake
add_library(jojo_core STATIC
  # existing sources...
  src/core/mips_decoder.cpp
)
add_jojo_test(jojo_mips_decoder_tests tests/test_mips_decoder.cpp)
```

- [ ] **Step 2: Run the test to verify RED**

Run:

```bash
cmake -S . -B build
cmake --build build --target jojo_mips_decoder_tests -j2
```

Expected: compilation fails because `core/mips_decoder.h`/`decode_mips` do not exist yet.

- [ ] **Step 3: Implement the typed decoder**

Create `src/core/mips_decoder.h` with the complete M2 operation set:

```cpp
#pragma once
#include <cstdint>

namespace jojo {

enum class MipsOp : std::uint16_t {
    reserved,
    sll, srl, sra, sllv, srlv, srav,
    add, addu, sub, subu, bit_and, bit_or, bit_xor, bit_nor, slt, sltu,
    addi, addiu, andi, ori, xori, lui, slti, sltiu,
    mfhi, mthi, mflo, mtlo, mult, multu, div, divu,
    j, jal, jr, jalr,
    beq, bne, blez, bgtz, bltz, bgez, bltzal, bgezal,
    lb, lbu, lh, lhu, lw, sb, sh, sw, lwl, lwr, swl, swr,
    syscall, break_,
    mfc0, mtc0, rfe,
    mfc2, cfc2, mtc2, ctc2, cop2_command
};

struct MipsInstruction {
    std::uint32_t raw{};
    MipsOp op{MipsOp::reserved};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t rd{};
    std::uint8_t sa{};
    std::uint16_t immediate{};
    std::uint32_t target{};
};

[[nodiscard]] MipsInstruction decode_mips(std::uint32_t raw) noexcept;
[[nodiscard]] bool is_control_transfer(MipsOp op) noexcept;

}
```

Implement `src/core/mips_decoder.cpp` by extracting fields once and dispatching on primary opcode/funct/REGIMM/COP rs. Exact required mappings:

```cpp
MipsInstruction out{
    raw,
    MipsOp::reserved,
    static_cast<std::uint8_t>((raw >> 21) & 31u),
    static_cast<std::uint8_t>((raw >> 16) & 31u),
    static_cast<std::uint8_t>((raw >> 11) & 31u),
    static_cast<std::uint8_t>((raw >> 6) & 31u),
    static_cast<std::uint16_t>(raw & 0xffffu),
    raw & 0x03ffffffu
};
```

Primary opcode `0x00` dispatches funct values for shifts, ALU, HI/LO, JR/JALR, syscall/break. `0x01` dispatches REGIMM on `rt`. Primary opcodes `0x02..0x0F`, load/store opcodes `0x20..0x2E`, COP0 `0x10` and COP2 `0x12` map only the operations listed in `MipsOp`; every unmatched encoding remains `reserved`.

- [ ] **Step 4: Run decoder test GREEN**

Run:

```bash
cmake --build build --target jojo_mips_decoder_tests -j2
ctest --test-dir build -R jojo_mips_decoder_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/mips_decoder.* tests/test_mips_decoder.cpp
git commit -m "feat: add R3000A MIPS decoder"
```

---

### Task 2: CPU State, Bus Contract and Non-Trapping Integer Core

**Files:**
- Create: `src/core/r3000a_state.h`
- Create: `src/core/r3000a_bus.h`
- Create: `src/core/r3000a_diagnostics.h`
- Create: `src/core/r3000a_reference_executor.h`
- Create: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/mips_test_encode.h`
- Create: `tests/r3000a_test_bus.h`
- Create: `tests/test_r3000a_integer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MipsInstruction`, `R3000aState`, `R3000aBus`.
- Produces: `R3000aStepResult step_r3000a(R3000aState&, R3000aBus&) noexcept` plus reusable state/bus/diagnostic types.

- [ ] **Step 1: Write RED tests for fetch, PC progression, `$zero`, shifts, logical ALU and immediates**

Define instruction helpers in `tests/mips_test_encode.h`:

```cpp
#pragma once
#include <cstdint>

namespace test_mips {
constexpr std::uint32_t r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                          std::uint8_t sa, std::uint8_t funct) {
    return (std::uint32_t(rs) << 21) | (std::uint32_t(rt) << 16) |
           (std::uint32_t(rd) << 11) | (std::uint32_t(sa) << 6) | funct;
}
constexpr std::uint32_t i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                          std::uint16_t imm) {
    return (std::uint32_t(op) << 26) | (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) | imm;
}
constexpr std::uint32_t j(std::uint8_t op, std::uint32_t target) {
    return (std::uint32_t(op) << 26) | (target & 0x03ffffffu);
}
}
```

Create a synthetic sparse little-endian bus in `tests/r3000a_test_bus.h` implementing the production interface and helpers `store8`, `store16`, `store32`, `peek32`, `fail_bus_error(address)`, and `fail_unsupported(address)`.

The integer test must include:

```cpp
TestR3000aBus bus;
jojo::R3000aState s{};
s.pc = 0x1000u;
s.next_pc = 0x1004u;
s.gpr[1] = 7u;
s.gpr[2] = 9u;
s.gpr[0] = 0xDEADBEEFu;
bus.store32(0x1000u, test_mips::r(1, 2, 3, 0, 0x21)); // ADDU

auto step = jojo::step_r3000a(s, bus);
CHECK(step.status == jojo::R3000aStepStatus::retired);
CHECK(s.gpr[3] == 16u);
CHECK(s.gpr[0] == 0u);
CHECK(s.pc == 0x1004u && s.next_pc == 0x1008u);
```

Add table cases for `SLL/SRL/SRA`, variable shifts masking count with `&31`, `AND/OR/XOR/NOR`, `SLT/SLTU`, `ADDU/SUBU`, `ADDIU`, `ANDI/ORI/XORI`, `LUI`, `SLTI`, and `SLTIU` with immediate `0xFFFF` proving sign-extension before unsigned comparison.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_integer_tests -j2
```

Expected: compile failure because the R3000A interfaces do not exist.

- [ ] **Step 3: Implement state, bus and step result types**

`src/core/r3000a_state.h`:

```cpp
#pragma once
#include <array>
#include <cstdint>

namespace jojo {
struct R3000aDelayedLoad { bool valid{}; std::uint8_t reg{}; std::uint32_t value{}; };
struct R3000aDelaySlot {
    bool active{};
    std::uint32_t branch_pc{};
    bool taken{};
    std::uint32_t target{};
};
struct R3000aCop0 {
    std::uint32_t target_address{}; // CP0 r6
    std::uint32_t bad_vaddr{};      // CP0 r8
    std::uint32_t status{};         // CP0 r12
    std::uint32_t cause{};          // CP0 r13
    std::uint32_t epc{};            // CP0 r14
};
struct R3000aState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t pc{};
    std::uint32_t next_pc{};
    R3000aDelayedLoad pending_load{};
    R3000aDelaySlot delay_slot{};
    R3000aCop0 cop0{};
    std::uint8_t external_interrupt_pending{}; // maps to Cause.IP[15:8]
};
}
```

`src/core/r3000a_bus.h`:

```cpp
#pragma once
#include <cstdint>

namespace jojo {
enum class R3000aBusStatus : std::uint8_t { ok, bus_error, unsupported };
struct R3000aBusResult {
    R3000aBusStatus status{R3000aBusStatus::ok};
    std::uint32_t value{};
};
class R3000aBus {
public:
    virtual ~R3000aBus() = default;
    virtual R3000aBusResult read8(std::uint32_t address) noexcept = 0;
    virtual R3000aBusResult read16(std::uint32_t address) noexcept = 0;
    virtual R3000aBusResult read32(std::uint32_t address) noexcept = 0;
    virtual R3000aBusResult write8(std::uint32_t address, std::uint8_t value) noexcept = 0;
    virtual R3000aBusResult write16(std::uint32_t address, std::uint16_t value) noexcept = 0;
    virtual R3000aBusResult write32(std::uint32_t address, std::uint32_t value) noexcept = 0;
};
}
```

`src/core/r3000a_diagnostics.h` must define architectural exception values and non-architectural boundaries separately:

```cpp
enum class R3000aExceptionCode : std::uint8_t {
    interrupt = 0, adel = 4, ades = 5, ibe = 6, dbe = 7,
    syscall = 8, breakpoint = 9, reserved_instruction = 10,
    coprocessor_unusable = 11, overflow = 12
};
enum class R3000aBoundaryCode : std::uint8_t {
    none, unsupported_address_space, architectural_operation_unimplemented,
    cop2_unimplemented, unpredictable_delay_slot_control_transfer
};
enum class R3000aStepStatus : std::uint8_t { retired, exception, boundary };
```

Use `std::optional` fields in `R3000aDiagnostic` for `opcode`, `address`, `access_width`, `write_value`, `exception_code`, `coprocessor`, and `register_index`; always populate `pc` and a stable `stage` enum.

- [ ] **Step 4: Implement minimal fetch + non-trapping integer execution**

`r3000a_reference_executor.cpp` must fetch with `bus.read32(state.pc)`, decode, capture all source operands before writes, advance `pc/next_pc`, apply one direct GPR write, then force `gpr[0]=0`.

Use explicit unsigned operations for wraparound and safe signed comparisons via `std::bit_cast<std::int32_t>`:

```cpp
const auto signed_rs = std::bit_cast<std::int32_t>(rs_value);
const auto signed_rt = std::bit_cast<std::int32_t>(rt_value);
const auto simm = static_cast<std::int32_t>(static_cast<std::int16_t>(ins.immediate));
```

Do not implement trapping `ADD/ADDI/SUB` in this task; recognize them and return `architectural_operation_unimplemented` until Task 3 adds exception machinery.

- [ ] **Step 5: Run GREEN**

```bash
cmake --build build --target jojo_r3000a_integer_tests -j2
ctest --test-dir build -R jojo_r3000a_integer_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_* tests/mips_test_encode.h tests/r3000a_test_bus.h tests/test_r3000a_integer.cpp
git commit -m "feat: add R3000A integer reference core"
```

---

### Task 3: Precise Exception Entry and Trapping Instructions

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Modify: `src/core/r3000a_diagnostics.h`
- Create: `tests/test_r3000a_exceptions.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: CPU state + bus faults + decoded trapping operations.
- Produces: architectural exception entry with `Cause`, `EPC`, `BadVAddr`, status-stack push and BEV-selected vector.

- [ ] **Step 1: Write RED exception tests**

Cover outside-delay-slot cases first:

```cpp
s.pc = 0x1000u;
s.next_pc = 0x1004u;
s.cop0.status = 0u; // BEV=0
bus.store32(0x1000u, test_mips::r(1, 2, 3, 0, 0x20)); // ADD
s.gpr[1] = 0x7fffffffu;
s.gpr[2] = 1u;
auto r = jojo::step_r3000a(s, bus);
CHECK(r.status == jojo::R3000aStepStatus::exception);
CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::overflow);
CHECK(s.gpr[3] == 0u);
CHECK(s.cop0.epc == 0x1000u);
CHECK(s.pc == 0x80000080u && s.next_pc == 0x80000084u);
```

Add cases for `ADDI`, `SUB`, `SYSCALL`, `BREAK`, reserved encoding -> RI, misaligned instruction fetch -> AdEL/BadVAddr, fetch bus error -> IBE, and BEV=1 -> `0xBFC00180`.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_exception_tests -j2
ctest --test-dir build -R jojo_r3000a_exception_tests --output-on-failure
```

Expected: FAIL because exception entry/trapping operations are not implemented.

- [ ] **Step 3: Implement exception entry helper**

Use one internal helper:

```cpp
R3000aStepResult enter_exception(R3000aState& s,
                                 R3000aExceptionCode code,
                                 std::uint32_t fault_pc,
                                 std::optional<std::uint32_t> bad_vaddr,
                                 std::optional<std::uint8_t> coprocessor) noexcept;
```

Outside delay slots it must:

```cpp
s.cop0.epc = fault_pc;
s.cop0.cause &= ~((0x1fu << 2) | (1u << 31) | (1u << 30));
s.cop0.cause |= (std::uint32_t(code) & 0x1fu) << 2;
if (bad_vaddr) s.cop0.bad_vaddr = *bad_vaddr;
const std::uint32_t low = s.cop0.status & 0x3fu;
s.cop0.status = (s.cop0.status & ~0x3fu) | ((low << 2) & 0x3fu);
s.pc = (s.cop0.status & (1u << 22)) ? 0xBFC00180u : 0x80000080u;
s.next_pc = s.pc + 4u;
s.delay_slot = {};
```

Trapping signed overflow must be detected without performing overflowing host signed arithmetic. Use sign-bit predicates on `std::uint32_t` operands/results.

A reserved decoder result invokes architectural RI. Misaligned fetch checks `state.pc & 3u` before calling `bus.read32`.

Map `R3000aBusStatus::bus_error` on instruction fetch to IBE. Map `unsupported` to boundary `unsupported_address_space`, preserving `pc` and fetch address.

- [ ] **Step 4: Run GREEN and regression suite**

```bash
cmake --build build -j2
ctest --test-dir build -R "jojo_r3000a_(integer|exception)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp src/core/r3000a_diagnostics.h tests/test_r3000a_exceptions.cpp
git commit -m "feat: add R3000A exception entry"
```

---

### Task 4: HI/LO Multiply and Divide Semantics

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_hilo.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MFHI/MTHI/MFLO/MTLO/MULT/MULTU/DIV/DIVU` decoder operations.
- Produces: deterministic HI/LO values with R3000A divide edge cases and no host UB.

- [ ] **Step 1: Write RED table tests**

Test signed/unsigned multiplication and these required divide edges:

```cpp
struct DivCase { std::uint32_t lhs, rhs, hi, lo; bool signed_div; };
const DivCase cases[] = {
    {7u, 0u, 7u, 0xffffffffu, false},
    {7u, 0u, 7u, 0xffffffffu, true},
    {0xfffffff9u, 0u, 0xfffffff9u, 1u, true},
    {0x80000000u, 0xffffffffu, 0u, 0x80000000u, true},
};
```

Also prove `MFHI/MFLO` write a GPR, `MTHI/MTLO` capture pre-step source values, and writes to `$zero` remain discarded.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_hilo_tests -j2
ctest --test-dir build -R jojo_r3000a_hilo_tests --output-on-failure
```

Expected: FAIL/boundary because HI/LO operations are not executed yet.

- [ ] **Step 3: Implement HI/LO safely**

Use `std::int64_t`/`std::uint64_t` for multiplication. Handle divide edge cases before any host division:

```cpp
if (divisor == 0u) { /* exact spec table */ }
else if (signed_div && dividend == 0x80000000u && divisor == 0xffffffffu) {
    s.hi = 0u;
    s.lo = 0x80000000u;
} else {
    // safe normal division after exclusions
}
```

Do not model multiply/divide cycle stalls in M2.

- [ ] **Step 4: Run GREEN**

```bash
ctest --test-dir build -R jojo_r3000a_hilo_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_hilo.cpp
git commit -m "feat: add R3000A HI LO semantics"
```

---

### Task 5: Branches, Jumps, Link Values and Delay Slots

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_control_flow.cpp`
- Extend: `tests/test_r3000a_exceptions.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `state.pc`, `state.next_pc`, captured source GPRs and `R3000aDelaySlot`.
- Produces: exact branch/jump target scheduling, link writes, delay-slot execution and delay-slot exception context.

- [ ] **Step 1: Write RED control-flow tests**

Add cases for taken and not-taken `BEQ/BNE/BLEZ/BGTZ/BLTZ/BGEZ`, `J/JAL/JR/JALR`, and link variants. Prove the delay slot executes before target transfer:

```cpp
bus.store32(0x1000u, test_mips::i(0x04, 1, 1, 2));          // BEQ -> 0x100C
bus.store32(0x1004u, test_mips::i(0x09, 2, 2, 1));          // ADDIU $2,$2,1 delay slot
bus.store32(0x100Cu, test_mips::i(0x09, 3, 3, 1));
s.gpr[2] = 10u;
CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
CHECK(s.pc == 0x1004u && s.next_pc == 0x100Cu);
CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
CHECK(s.gpr[2] == 11u);
CHECK(s.pc == 0x100Cu);
```

Test `JAL/JALR` link = `branch_pc + 8`, `JALR rd==rs` uses pre-write source target, and `BLTZAL/BGEZAL` write `$ra` whether or not the condition is true.

Write a RED boundary case where a jump appears in an active delay slot and expect `unpredictable_delay_slot_control_transfer`.

Extend exception tests: faulting delay-slot instruction must set `Cause.BD`, set `EPC=branch_pc`, and for taken/unconditional transfer set `Cause.BT` plus `cop0.target_address`.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_control_flow_tests jojo_r3000a_exception_tests -j2
ctest --test-dir build -R "jojo_r3000a_(control_flow|exception)_tests" --output-on-failure
```

Expected: FAIL because branches currently do not schedule an architectural delay slot.

- [ ] **Step 3: Implement delay-slot scheduling**

At instruction start capture whether the current instruction is already a delay-slot instruction:

```cpp
const auto current_delay = s.delay_slot;
const bool in_delay_slot = current_delay.active;
```

For a control transfer outside a delay slot, compute `branch_target`, `taken`, and link writes from captured source values, advance to sequential `pc+4`, then leave:

```cpp
s.delay_slot = R3000aDelaySlot{true, instruction_pc, taken, branch_target};
s.pc = sequential_pc;
s.next_pc = taken ? branch_target : sequential_pc + 4u;
```

After executing the actual delay-slot instruction, clear `delay_slot`. A control transfer while `in_delay_slot` returns the boundary diagnostic before changing branch state.

Update exception entry so an exception from `current_delay.active` sets BD bit31, EPC to `current_delay.branch_pc`, BT bit30 only when `current_delay.taken`, and `target_address=current_delay.target` when BT is set.

- [ ] **Step 4: Run GREEN**

```bash
ctest --test-dir build -R "jojo_r3000a_(control_flow|exception|integer)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_control_flow.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: model R3000A delay slots"
```

---

### Task 6: Aligned Memory Operations and One-Instruction Load Delay

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_memory.cpp`
- Extend: `tests/r3000a_test_bus.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LB/LBU/LH/LHU/LW/SB/SH/SW`, effective addresses and bus results.
- Produces: aligned bus accesses, sign/zero extension, AdEL/AdES/DBE boundaries and `R3000aDelayedLoad` retirement.

- [ ] **Step 1: Write RED memory/load-delay tests**

Test little-endian load/store widths and sign extension. Lock the load-delay sequence:

```cpp
s.gpr[4] = 0x2000u;
s.gpr[8] = 0x11111111u;
bus.store32(0x2000u, 0xAABBCCDDu);
bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));          // LW $t0,0($a0)
bus.store32(0x1004u, test_mips::r(8, 0, 9, 0, 0x21));      // ADDU $t1,$t0,$zero
bus.store32(0x1008u, test_mips::r(8, 0, 10, 0, 0x21));     // ADDU $t2,$t0,$zero
jojo::step_r3000a(s, bus);
jojo::step_r3000a(s, bus);
CHECK(s.gpr[9] == 0x11111111u);
jojo::step_r3000a(s, bus);
CHECK(s.gpr[10] == 0xAABBCCDDu);
```

Add same-destination overwrite: `LW $t0,...` followed by `ADDIU $t0,$zero,7` leaves `$t0=7`. Add load to `$zero` access/fault behavior with no pending state.

Add alignment cases: `LH/LHU/SH` odd -> AdEL/AdES; `LW/SW` non-4-aligned -> AdEL/AdES. Data bus error -> DBE. Unsupported bus result -> `unsupported_address_space` boundary rather than zero read.

Add an exception-after-load case proving the older pending load retires before handler observation.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_memory_tests -j2
ctest --test-dir build -R jojo_r3000a_memory_tests --output-on-failure
```

Expected: FAIL because memory operations/load pipeline are not implemented.

- [ ] **Step 3: Implement load retirement order**

At each normal instruction, capture operands before retiring `state.pending_load`. After current semantics are known:

```cpp
const auto old_pending = s.pending_load;
// current instruction sources already captured here
if (old_pending.valid && old_pending.reg != 0u) s.gpr[old_pending.reg] = old_pending.value;
if (direct_write.valid && direct_write.reg != 0u) s.gpr[direct_write.reg] = direct_write.value;
s.pending_load = new_pending;
s.gpr[0] = 0u;
```

A current faulting load does not create `new_pending`. Before architectural exception entry caused by the instruction after a successful load, retire `old_pending` first.

Use `read8/read16/read32` and `write8/write16/write32` only after alignment checks. Sign extension uses explicit casts from `std::int8_t`/`std::int16_t` into `std::int32_t`, then bit-preserving conversion to `std::uint32_t`.

- [ ] **Step 4: Run GREEN**

```bash
ctest --test-dir build -R "jojo_r3000a_(memory|exception|integer)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/r3000a_test_bus.h tests/test_r3000a_memory.cpp
git commit -m "feat: add R3000A memory load delay"
```

---

### Task 7: LWL/LWR/SWL/SWR Merge Semantics

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_unaligned.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: low two effective-address bits, aligned 32-bit bus word, visible/pending target register.
- Produces: exact little-endian merge values; LWL/LWR schedule delayed writes and forward a pending same-register merge value.

- [ ] **Step 1: Write RED byte-lane tables**

For little-endian memory word `0x44332211` and register seed `0xAABBCCDD`, lock all four low-address values with explicit expected tables:

```cpp
const std::uint32_t lwl_expected[4] = {
    0x11BBCCDDu, 0x2211CCDDu, 0x332211DDu, 0x44332211u
};
const std::uint32_t lwr_expected[4] = {
    0x44332211u, 0xAA443322u, 0xAABB4433u, 0xAABBCC44u
};
```

For stores, initialize memory word `0x44332211` and source GPR `0xAABBCCDD`; lock the inverse little-endian SWL/SWR lane behavior for all four offsets by checking the final aligned word. Include back-to-back `LWL/LWR` to the same `rt` and prove the second merge uses the first pending value, while an ordinary ALU consumer still sees the pre-load GPR until the merged pending value retires.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_unaligned_tests -j2
ctest --test-dir build -R jojo_r3000a_unaligned_tests --output-on-failure
```

Expected: FAIL because merge loads/stores are not implemented.

- [ ] **Step 3: Implement aligned-word merge helpers**

Use `aligned = effective & ~3u` and `shift = effective & 3u`. Implement LWL/LWR with explicit per-offset switch values matching the test tables rather than host-endian pointer tricks.

Before LWL/LWR merge:

```cpp
std::uint32_t merge_base = captured_rt;
if (s.pending_load.valid && s.pending_load.reg == ins.rt)
    merge_base = s.pending_load.value;
```

The merged result becomes the new pending load. Do not expose that forwarding to arithmetic instructions.

For SWL/SWR, read the aligned backing word, merge source bytes by offset, then write the full aligned word through `bus.write32`. Preserve explicit bus-error/unsupported diagnostics.

- [ ] **Step 4: Run GREEN**

```bash
ctest --test-dir build -R "jojo_r3000a_(unaligned|memory)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_unaligned.cpp
git commit -m "feat: add R3000A unaligned word merges"
```

---

### Task 8: COP0, RFE and Interrupt Admission

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_cop0.cpp`
- Extend: `tests/test_r3000a_exceptions.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: COP0 register indices 6/8/12/13/14, `MFC0/MTC0/RFE`, status/cause masks and `external_interrupt_pending`.
- Produces: delayed MFC0 GPR writes, masked MTC0 writes, status-stack restore and legal-boundary interrupt entry.

- [ ] **Step 1: Write RED COP0 tests**

Use these supported register numbers:

```text
6  TargetAddress
8  BadVAddr
12 Status
13 Cause
14 EPC
```

Lock masks as constants in production and tests:

```cpp
constexpr std::uint32_t kStatusWritableMask = 0xF27FFF3Fu;
constexpr std::uint32_t kCauseSoftwareInterruptMask = 0x00000300u;
```

Test `MFC0` one-instruction delayed visibility, `MTC0 Status` preserving non-writable bits, `MTC0 Cause` changing only software-interrupt pending bits, writable EPC, read-only BadVAddr/TargetAddress policy according to the spec contract, and RI for unavailable COP0 register access/commands.

Test RFE only rotates low status stack bits and does not jump to EPC:

```cpp
const auto pc_before = s.pc;
// after executing RFE, normal PC progression applies; no EPC jump
CHECK((s.cop0.status & 0x0fu) == expected_restored_low_bits);
CHECK(s.pc == pc_before + 4u);
```

- [ ] **Step 2: Write RED interrupt tests**

Set `external_interrupt_pending` and matching `Status.IM`, with current IE enabled. Prove interrupt is accepted before the next ordinary instruction when no delay slot is pending. Disable IE or mask the line and prove instruction execution proceeds.

Issue a branch, then assert an interrupt before its delay slot: the delay slot must execute and interrupt recognition is deferred until the following legal boundary. Also prove a pending delayed GPR load retires before the interrupt handler begins.

- [ ] **Step 3: Run RED**

```bash
cmake --build build --target jojo_r3000a_cop0_tests -j2
ctest --test-dir build -R "jojo_r3000a_(cop0|exception)_tests" --output-on-failure
```

Expected: FAIL because COP0 and interrupt admission are not implemented.

- [ ] **Step 4: Implement COP0 and interrupt logic**

Centralize constants:

```cpp
constexpr std::uint32_t kStatusWritableMask = 0xF27FFF3Fu;
constexpr std::uint32_t kCauseSoftwareInterruptMask = 0x00000300u;
constexpr std::uint32_t kCauseBd = 1u << 31;
constexpr std::uint32_t kCauseBt = 1u << 30;
```

`MFC0` uses the same `new_pending` path as memory loads. `MTC0` applies register-specific masks; unsupported registers enter RI rather than returning fabricated values.

`RFE` performs:

```cpp
const std::uint32_t low = s.cop0.status & 0x3fu;
const std::uint32_t restored = (low & 0x30u) | ((low >> 2) & 0x0fu);
s.cop0.status = (s.cop0.status & ~0x3fu) | restored;
```

Before fetch, synchronize external pending lines into Cause IP bits while preserving software pending bits. Accept interrupt only when current IE is set and `(Cause.IP & Status.IM) != 0`. Do not accept while `state.delay_slot.active`; defer until that slot retires. Before accepted interrupt exception entry, retire any older pending GPR load.

- [ ] **Step 5: Run GREEN**

```bash
ctest --test-dir build -R "jojo_r3000a_(cop0|control_flow|memory|exception)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_cop0.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: add R3000A COP0 interrupts"
```

---

### Task 9: COP2/GTE Boundary and PS-X EXE CPU Initialization

**Files:**
- Modify: `src/core/r3000a_reference_executor.h`
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_boundary.cpp`
- Create: `tests/test_r3000a_init.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: decoded COP2 classes and existing `Ps1ExeMetadata` from `core/ps1_exe.h`.
- Produces: CpU vs `cop2_unimplemented` distinction and `R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata&) noexcept`.

- [ ] **Step 1: Write RED COP2 boundary tests**

With COP2 disabled in Status, execute a COP2 transfer/command and expect architectural CpU with coprocessor=2. With COP2 enabled, expect boundary `cop2_unimplemented`, preserving `pc` and raw `opcode`. Confirm no COP2 operation retires as a NOP.

- [ ] **Step 2: Write RED PS-X EXE initialization tests**

Use synthetic metadata only:

```cpp
jojo::Ps1ExeMetadata m{};
m.entry_pc = 0x80010000u;
m.initial_gp = 0x80018000u;
m.stack_base = 0x801FFF00u;
m.stack_size = 0x100u;
auto s = jojo::initialize_r3000a_for_psx_exe(m);
CHECK(s.pc == 0x80010000u);
CHECK(s.next_pc == 0x80010004u);
CHECK(s.gpr[28] == 0x80018000u);
CHECK(s.gpr[29] == 0x80200000u);
CHECK(s.gpr[0] == 0u);
CHECK(!s.pending_load.valid && !s.delay_slot.active);
```

Add a zero-stack case where SP remains zero. HI/LO/COP0/pending state must initialize deterministically to zero.

- [ ] **Step 3: Run RED**

```bash
cmake --build build --target jojo_r3000a_boundary_tests jojo_r3000a_init_tests -j2
ctest --test-dir build -R "jojo_r3000a_(boundary|init)_tests" --output-on-failure
```

Expected: FAIL because COP2 gating and the initializer do not exist.

- [ ] **Step 4: Implement COP2 gate and initializer**

Expose in `r3000a_reference_executor.h`:

```cpp
[[nodiscard]] R3000aStepResult step_r3000a(R3000aState&, R3000aBus&) noexcept;
[[nodiscard]] R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata&) noexcept;
```

Use Status CU2 bit30 for the M2 COP2 enable gate. Disabled -> exception `coprocessor_unusable` with CE=2 context; enabled -> boundary `cop2_unimplemented`.

Initializer:

```cpp
R3000aState s{};
s.pc = m.entry_pc;
s.next_pc = m.entry_pc + 4u;
s.gpr[28] = m.initial_gp;
if (m.stack_base != 0u || m.stack_size != 0u)
    s.gpr[29] = m.stack_base + m.stack_size; // defined uint32_t wrap
s.gpr[0] = 0u;
return s;
```

Do not load commercial text bytes or call BIOS/HLE here; memory preparation belongs to the next milestone.

- [ ] **Step 5: Run GREEN**

```bash
ctest --test-dir build -R "jojo_r3000a_(boundary|init|cop0)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.* tests/test_r3000a_boundary.cpp tests/test_r3000a_init.cpp
git commit -m "feat: add R3000A COP2 boundary init"
```

---

### Task 10: Determinism, Full CI Gate and Truthful M2 Status

**Files:**
- Extend: `tests/test_r3000a_init.cpp`
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`
- Verify: `cmake/CheckPs1ActiveArchitecture.cmake`
- Verify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: all M2 semantics from Tasks 1-9.
- Produces: end-to-end synthetic reference-core evidence and truthful documentation that the next milestone is PS1 memory/bus + BIOS/HLE.

- [ ] **Step 1: Add deterministic replay RED/GREEN contract**

Extend `tests/test_r3000a_init.cpp` with a synthetic instruction sequence executed twice from identical CPU/bus snapshots. Compare every architectural field and every structured step status/diagnostic sequence:

```cpp
CHECK(a.gpr == b.gpr);
CHECK(a.hi == b.hi && a.lo == b.lo);
CHECK(a.pc == b.pc && a.next_pc == b.next_pc);
CHECK(a.cop0.status == b.cop0.status);
CHECK(a.cop0.cause == b.cop0.cause);
CHECK(a.cop0.epc == b.cop0.epc);
CHECK(a.cop0.bad_vaddr == b.cop0.bad_vaddr);
CHECK(a.cop0.target_address == b.cop0.target_address);
CHECK(a.pending_load.valid == b.pending_load.valid);
CHECK(a.delay_slot.active == b.delay_slot.active);
```

The synthetic sequence must include at least one delayed load, ALU consumer, branch+delay slot, HI/LO operation and non-faulting COP0 access.

- [ ] **Step 2: Run the complete local suite**

Linux-style command:

```bash
cmake -S . -B build
cmake --build build -j2
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
```

Expected: all tests PASS and PS1 architecture gate PASS.

On Windows/MSVC the authoritative CI commands remain those in `.github/workflows/build.yml`; do not add a second bespoke CPU workflow unless the existing full build cannot execute the new CTest targets.

- [ ] **Step 3: Update truthful status documents only after local/full branch verification**

`PROJECT-STATE.md` must say, in substance and without stronger claims:

```text
M2 R3000A reference core: implemented on the feature branch and verified by synthetic Linux/Windows CPU contracts.
reference-execution-ready refers only to CPU semantic readiness.
Commercial JoJo boot/render/audio/input/gameplay remain unverified.
Next: JoJo-specific PS1 memory/bus + BIOS/HLE foundation.
```

`docs/architecture/PRODUCTION-READINESS.tsv` must add/update a machine-readable row for the R3000A reference-core work with the final GitHub Actions run ID as evidence after that run exists. Do not mark boot/render/gameplay rows verified.

`docs/NEXT-MILESTONES.md` must name PS1 memory/bus + BIOS/HLE as the next architectural cycle; CFG/IR/x64 remains downstream of the reference oracle.

- [ ] **Step 4: Push branch and require Linux + Windows CI**

Run the normal push for the execution branch. Then inspect the workflow run tied to the exact head SHA and require both jobs to pass:

```text
Portable core / Linux: success
Windows x64 / MSVC 2022: success
PS1 active architecture gate: success
CTest: success
```

If either platform fails, use systematic-debugging and do not mark M2 complete.

- [ ] **Step 5: Replace the provisional readiness evidence with the exact successful run ID**

After the successful run exists, update only the evidence field in `docs/architecture/PRODUCTION-READINESS.tsv` and any matching `PROJECT-STATE.md` checkpoint line. Push again and require the documentation-only head to remain green on Linux and Windows.

- [ ] **Step 6: Final verification checklist**

Confirm all of the following from the exact final head:

```text
MIPS decoder tests green
integer/$zero/immediate tests green
overflow/exception/RI/bus-fault tests green
HI/LO edge tests green
branch/jump/delay-slot tests green
load-delay/aligned-memory tests green
LWL/LWR/SWL/SWR tests green
COP0/RFE/interrupt tests green
COP2 boundary tests green
PS-X EXE initialization tests green
deterministic replay green
PS1 architecture gate green
Linux CI green
Windows CI green
no boot/render/audio/input/gameplay claim promoted
```

- [ ] **Step 7: Commit final status**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv tests/test_r3000a_init.cpp
git commit -m "docs: verify R3000A reference core M2"
```

---

## Plan Completion Boundary

M2 is complete only when every task above is implemented, the entire project test suite is green on Linux and Windows for the exact final head, the PS1 architecture gate remains green, and status documents describe only **reference CPU semantic readiness**.

M2 completion does not mean the JoJo commercial executable boots. The next independent design/spec cycle is JoJo-specific **PS1 memory/bus + BIOS/HLE foundation**. MIPS CFG, PS1 IR and Windows x64 lowering remain downstream and must use this reference executor as their semantic oracle.