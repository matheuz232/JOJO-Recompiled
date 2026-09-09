# R3000A Reference Core M2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic PS1 R3000A/MIPS-I reference executor that becomes the semantic oracle for later JoJo CFG/IR/x64 work, without claiming commercial boot or implementing PS1 devices.

**Architecture:** A pure decoder turns one 32-bit MIPS word into a typed operation. A one-step reference executor mutates explicit R3000A state, routes every guest memory access through an abstract bus, and returns a structured step result that separates architectural exceptions from implementation boundaries. Delay slots, delayed GPR loads, COP0 state, interrupt admission and COP2/GTE boundaries are explicit state transitions.

**Tech Stack:** C++20, CMake 3.20+, CTest, MSVC 2022 x64, GCC/Clang-compatible portable core, existing `jojo_core` static library.

**Spec:** `docs/superpowers/specs/2026-09-09-r3000a-reference-core-m2-design.md`

## Global Constraints

- Guest platform remains Sony PlayStation 1 only; never reintroduce Dreamcast/SH-4 guest code.
- Scope remains this JoJo PS1 title/revision family, not a general PlayStation emulator.
- No commercial game image, PS-X EXE, BIOS, extracted asset or derived commercial byte enters source, tests, CI or release artifacts.
- Guest integer arithmetic and addresses are exactly 32-bit; host signed-overflow behavior may not define guest semantics.
- Executor code never dereferences guest addresses as host pointers; all memory traffic crosses `R3000aBus`.
- `$zero` is forced to zero at each public step boundary.
- Reserved encodings raise architectural RI. Recognized operations deliberately outside M2 return a structured boundary.
- A control transfer encountered in an already active delay slot returns `unpredictable_delay_slot_control_transfer`.
- COP2/GTE arithmetic stays out of scope. Disabled COP2 raises CpU; enabled COP2 returns `cop2_unimplemented`.
- Synthetic CPU tests do not establish `boot-reached`, rendering, audio, input or gameplay compatibility.
- M2 completion requires the exact final head to pass Linux and Windows/MSVC CI plus the active PS1 architecture gate.

## Locked File Structure

Create:

- `src/core/mips_decoder.h/.cpp` — pure fixed-width MIPS-I decode.
- `src/core/r3000a_state.h` — architectural CPU/COP0/delay state.
- `src/core/r3000a_bus.h` — abstract typed bus boundary.
- `src/core/r3000a_diagnostics.h` — step status, exception codes and structured diagnostics.
- `src/core/r3000a_reference_executor.h/.cpp` — one-step semantic executor plus PS-X EXE initializer.
- `tests/mips_test_encode.h` — synthetic R/I/J encoders.
- `tests/r3000a_test_bus.h` — sparse deterministic little-endian synthetic bus.
- `tests/test_mips_decoder.cpp`
- `tests/test_r3000a_integer.cpp`
- `tests/test_r3000a_exceptions.cpp`
- `tests/test_r3000a_hilo.cpp`
- `tests/test_r3000a_control_flow.cpp`
- `tests/test_r3000a_memory.cpp`
- `tests/test_r3000a_unaligned.cpp`
- `tests/test_r3000a_cop0.cpp`
- `tests/test_r3000a_boundary.cpp`
- `tests/test_r3000a_init.cpp`

Modify:

- `CMakeLists.txt`
- `PROJECT-STATE.md`
- `docs/NEXT-MILESTONES.md`
- `docs/architecture/PRODUCTION-READINESS.tsv`

---

### Task 1: Pure MIPS-I Decoder

**Files:**
- Create: `src/core/mips_decoder.h`
- Create: `src/core/mips_decoder.cpp`
- Create: `tests/test_mips_decoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: one `std::uint32_t` raw word.
- Produces: `MipsInstruction decode_mips(std::uint32_t) noexcept` and `bool is_control_transfer(MipsOp) noexcept`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "core/mips_decoder.h"
#include <iostream>
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)
int main() {
    auto a = jojo::decode_mips(0x012A4021u); // addu $t0,$t1,$t2
    CHECK(a.op == jojo::MipsOp::addu && a.rs == 9u && a.rt == 10u && a.rd == 8u);
    CHECK(jojo::decode_mips(0x2528FFF0u).op == jojo::MipsOp::addiu);
    CHECK(jojo::decode_mips(0x0C004000u).op == jojo::MipsOp::jal);
    CHECK(jojo::decode_mips(0x04100000u).op == jojo::MipsOp::bltzal);
    CHECK(jojo::decode_mips(0x42000010u).op == jojo::MipsOp::rfe);
    CHECK(jojo::decode_mips(0x48000000u).op == jojo::MipsOp::mfc2);
    CHECK(jojo::decode_mips(0x4A000000u).op == jojo::MipsOp::cop2_command);
    CHECK(jojo::decode_mips(0x70000000u).op == jojo::MipsOp::reserved);
    return failures ? 1 : 0;
}
```

Register `jojo_mips_decoder_tests` in CMake.

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build
cmake --build build --target jojo_mips_decoder_tests -j2
```

Expected: compile failure because decoder files do not exist.

- [ ] **Step 3: Implement the decoder contract**

`src/core/mips_decoder.h`:

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
    syscall, break_, mfc0, mtc0, rfe,
    mfc2, cfc2, mtc2, ctc2, cop2_command
};
struct MipsInstruction {
    std::uint32_t raw{};
    MipsOp op{MipsOp::reserved};
    std::uint8_t rs{}, rt{}, rd{}, sa{};
    std::uint16_t immediate{};
    std::uint32_t target{};
};
[[nodiscard]] MipsInstruction decode_mips(std::uint32_t raw) noexcept;
[[nodiscard]] bool is_control_transfer(MipsOp op) noexcept;
}
```

`decode_mips` extracts fields exactly once:

```cpp
MipsInstruction out{
    raw, MipsOp::reserved,
    static_cast<std::uint8_t>((raw >> 21) & 31u),
    static_cast<std::uint8_t>((raw >> 16) & 31u),
    static_cast<std::uint8_t>((raw >> 11) & 31u),
    static_cast<std::uint8_t>((raw >> 6) & 31u),
    static_cast<std::uint16_t>(raw & 0xffffu),
    raw & 0x03ffffffu
};
```

Primary opcode `0x00` dispatches SPECIAL funct values; `0x01` dispatches REGIMM by `rt`; `0x02..0x0F` dispatch jumps/branches/immediates; `0x20..0x2E` dispatch loads/stores; `0x10` dispatches MFC0/MTC0/RFE; `0x12` dispatches MFC2/CFC2/MTC2/CTC2/COP2 command. Every unmatched encoding remains `reserved`.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build --target jojo_mips_decoder_tests -j2
ctest --test-dir build -R jojo_mips_decoder_tests --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/mips_decoder.* tests/test_mips_decoder.cpp
git commit -m "feat: add R3000A MIPS decoder"
```

---

### Task 2: State, Bus, Diagnostics and Non-Trapping Integer Core

**Files:**
- Create: `src/core/r3000a_state.h`
- Create: `src/core/r3000a_bus.h`
- Create: `src/core/r3000a_diagnostics.h`
- Create: `src/core/r3000a_reference_executor.h/.cpp`
- Create: `tests/mips_test_encode.h`
- Create: `tests/r3000a_test_bus.h`
- Create: `tests/test_r3000a_integer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: typed decoder output + abstract bus.
- Produces: `R3000aStepResult step_r3000a(R3000aState&, R3000aBus&) noexcept`.

- [ ] **Step 1: Write test helpers and failing integer tests**

`tests/mips_test_encode.h`:

```cpp
#pragma once
#include <cstdint>
namespace test_mips {
constexpr std::uint32_t r(std::uint8_t rs,std::uint8_t rt,std::uint8_t rd,std::uint8_t sa,std::uint8_t fn) {
    return (std::uint32_t(rs)<<21)|(std::uint32_t(rt)<<16)|(std::uint32_t(rd)<<11)|(std::uint32_t(sa)<<6)|fn;
}
constexpr std::uint32_t i(std::uint8_t op,std::uint8_t rs,std::uint8_t rt,std::uint16_t imm) {
    return (std::uint32_t(op)<<26)|(std::uint32_t(rs)<<21)|(std::uint32_t(rt)<<16)|imm;
}
constexpr std::uint32_t j(std::uint8_t op,std::uint32_t target) {
    return (std::uint32_t(op)<<26)|(target & 0x03ffffffu);
}
}
```

Create `TestR3000aBus` as a sparse byte map implementing the production bus and helpers `store8/16/32`, `peek32`, `fail_bus_error`, `fail_unsupported`.

Core RED:

```cpp
TestR3000aBus bus;
jojo::R3000aState s{};
s.pc=0x1000u; s.next_pc=0x1004u; s.gpr[1]=7u; s.gpr[2]=9u; s.gpr[0]=0xDEADBEEFu;
bus.store32(0x1000u, test_mips::r(1,2,3,0,0x21));
auto r = jojo::step_r3000a(s,bus);
CHECK(r.status == jojo::R3000aStepStatus::retired);
CHECK(s.gpr[3] == 16u && s.gpr[0] == 0u);
CHECK(s.pc == 0x1004u && s.next_pc == 0x1008u);
```

Add tables for SLL/SRL/SRA, SLLV/SRLV/SRAV with low-five-bit shift masking, ADDU/SUBU, AND/OR/XOR/NOR, SLT/SLTU, ADDIU, ANDI/ORI/XORI, LUI, SLTI and SLTIU with `imm=0xFFFF` proving sign-extension before unsigned comparison.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_integer_tests -j2
```

Expected: missing R3000A interfaces.

- [ ] **Step 3: Implement exact shared types**

`r3000a_state.h`:

```cpp
#pragma once
#include <array>
#include <cstdint>
namespace jojo {
struct R3000aDelayedLoad { bool valid{}; std::uint8_t reg{}; std::uint32_t value{}; };
struct R3000aDelaySlot { bool active{}; std::uint32_t branch_pc{}; bool taken{}; std::uint32_t target{}; };
struct R3000aCop0 { std::uint32_t target_address{}, bad_vaddr{}, status{}, cause{}, epc{}; };
struct R3000aState {
    std::array<std::uint32_t,32> gpr{};
    std::uint32_t hi{}, lo{}, pc{}, next_pc{};
    R3000aDelayedLoad pending_load{};
    R3000aDelaySlot delay_slot{};
    R3000aCop0 cop0{};
    std::uint8_t external_interrupt_pending{}; // bit2..7 map to Cause.IP10..15
};
}
```

`r3000a_bus.h`:

```cpp
#pragma once
#include <cstdint>
namespace jojo {
enum class R3000aBusStatus : std::uint8_t { ok, bus_error, unsupported };
struct R3000aBusResult { R3000aBusStatus status{R3000aBusStatus::ok}; std::uint32_t value{}; };
class R3000aBus {
public:
    virtual ~R3000aBus() = default;
    virtual R3000aBusResult read8(std::uint32_t) noexcept = 0;
    virtual R3000aBusResult read16(std::uint32_t) noexcept = 0;
    virtual R3000aBusResult read32(std::uint32_t) noexcept = 0;
    virtual R3000aBusResult write8(std::uint32_t,std::uint8_t) noexcept = 0;
    virtual R3000aBusResult write16(std::uint32_t,std::uint16_t) noexcept = 0;
    virtual R3000aBusResult write32(std::uint32_t,std::uint32_t) noexcept = 0;
};
}
```

`r3000a_diagnostics.h`:

```cpp
#pragma once
#include <cstdint>
#include <optional>
namespace jojo {
enum class R3000aExceptionCode : std::uint8_t {
    interrupt=0, adel=4, ades=5, ibe=6, dbe=7, syscall=8,
    breakpoint=9, reserved_instruction=10, coprocessor_unusable=11, overflow=12
};
enum class R3000aBoundaryCode : std::uint8_t {
    none, unsupported_address_space, architectural_operation_unimplemented,
    cop2_unimplemented, unpredictable_delay_slot_control_transfer
};
enum class R3000aStage : std::uint8_t { interrupt, fetch, decode, execute, memory, cop0, cop2 };
enum class R3000aStepStatus : std::uint8_t { retired, exception, boundary };
struct R3000aDiagnostic {
    R3000aBoundaryCode boundary{R3000aBoundaryCode::none};
    R3000aStage stage{R3000aStage::execute};
    std::uint32_t pc{};
    std::optional<std::uint32_t> opcode, address, write_value;
    std::optional<std::uint8_t> access_width, coprocessor, register_index;
    std::optional<R3000aExceptionCode> exception_code;
};
struct R3000aStepResult { R3000aStepStatus status{R3000aStepStatus::retired}; R3000aDiagnostic diagnostic{}; };
}
```

- [ ] **Step 4: Implement fetch and non-trapping ALU**

`step_r3000a` checks aligned fetch, calls `bus.read32(state.pc)`, decodes, captures source operands before writes, executes non-trapping integer operations, advances ordinary PC as `pc=next_pc; next_pc+=4`, and ends with `gpr[0]=0`.

Use explicit signed views:

```cpp
const auto srs = std::bit_cast<std::int32_t>(rs_value);
const auto srt = std::bit_cast<std::int32_t>(rt_value);
const auto simm = static_cast<std::int32_t>(static_cast<std::int16_t>(ins.immediate));
```

ADD/ADDI/SUB return `architectural_operation_unimplemented` until Task 3.

- [ ] **Step 5: Run GREEN**

```bash
cmake --build build --target jojo_r3000a_integer_tests -j2
ctest --test-dir build -R jojo_r3000a_integer_tests --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_* tests/mips_test_encode.h tests/r3000a_test_bus.h tests/test_r3000a_integer.cpp
git commit -m "feat: add R3000A integer reference core"
```

---

### Task 3: Precise Exceptions and Trapping ALU

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_exceptions.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: AdEL/AdES/IBE/DBE/Sys/Bp/RI/Ovf entry, BEV vectoring and status-stack push.

- [ ] **Step 1: Write RED tests**

```cpp
s.pc=0x1000u; s.next_pc=0x1004u; s.gpr[1]=0x7fffffffu; s.gpr[2]=1u;
bus.store32(0x1000u, test_mips::r(1,2,3,0,0x20));
auto r=jojo::step_r3000a(s,bus);
CHECK(r.status==jojo::R3000aStepStatus::exception);
CHECK(r.diagnostic.exception_code==jojo::R3000aExceptionCode::overflow);
CHECK(s.gpr[3]==0u && s.cop0.epc==0x1000u);
CHECK(s.pc==0x80000080u && s.next_pc==0x80000084u);
```

Add ADDI/SUB overflow, SYSCALL, BREAK, reserved encoding -> RI, misaligned fetch -> AdEL+BadVAddr, fetch bus error -> IBE, and BEV=1 -> `0xBFC00180`.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_exception_tests -j2
ctest --test-dir build -R jojo_r3000a_exception_tests --output-on-failure
```

- [ ] **Step 3: Implement exception entry**

Internal signature:

```cpp
R3000aStepResult enter_exception(R3000aState&, R3000aExceptionCode,
                                 std::uint32_t fault_pc,
                                 std::optional<std::uint32_t> bad_vaddr,
                                 std::optional<std::uint8_t> coprocessor) noexcept;
```

Outside a delay slot:

```cpp
s.cop0.epc=fault_pc;
s.cop0.cause &= ~((0x1fu<<2)|(3u<<28)|(1u<<30)|(1u<<31));
s.cop0.cause |= (std::uint32_t(code)&0x1fu)<<2;
if (coprocessor) s.cop0.cause |= (std::uint32_t(*coprocessor)&3u)<<28;
if (bad_vaddr) s.cop0.bad_vaddr=*bad_vaddr;
const auto low=s.cop0.status & 0x3fu;
s.cop0.status=(s.cop0.status & ~0x3fu)|((low<<2)&0x3fu);
s.pc=(s.cop0.status & (1u<<22))?0xBFC00180u:0x80000080u;
s.next_pc=s.pc+4u;
s.delay_slot={};
```

BadVAddr is supplied only for AdEL/AdES. Bus errors do not change it. Detect signed overflow with unsigned sign-bit predicates; never execute overflowing host signed arithmetic.

- [ ] **Step 4: Run GREEN**

```bash
ctest --test-dir build -R "jojo_r3000a_(integer|exception)_tests" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: add R3000A exception entry"
```

---

### Task 4: HI/LO Multiply and Divide

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_hilo.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write RED edge-case table**

```cpp
struct DivCase { std::uint32_t lhs,rhs,hi,lo; bool is_signed; };
const DivCase cases[]={{7u,0u,7u,0xffffffffu,false},{7u,0u,7u,0xffffffffu,true},
 {0xfffffff9u,0u,0xfffffff9u,1u,true},{0x80000000u,0xffffffffu,0u,0x80000000u,true}};
```

Also test MFHI/MFLO/MTHI/MTLO and normal MULT/MULTU/DIV/DIVU.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_hilo_tests -j2
ctest --test-dir build -R jojo_r3000a_hilo_tests --output-on-failure
```

- [ ] **Step 3: Implement without host UB**

Use 64-bit products. Handle divisor zero and `0x80000000 / 0xffffffff` before host division. Do not model multiply/divide cycle stalls.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_r3000a_hilo_tests --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_hilo.cpp
git commit -m "feat: add R3000A HI LO semantics"
```

---

### Task 5: Branches, Jumps and Delay Slots

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_control_flow.cpp`
- Modify: `tests/test_r3000a_exceptions.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write RED branch/delay tests**

```cpp
bus.store32(0x1000u,test_mips::i(0x04,1,1,2));
bus.store32(0x1004u,test_mips::i(0x09,2,2,1));
s.gpr[2]=10u;
jojo::step_r3000a(s,bus);
CHECK(s.pc==0x1004u && s.next_pc==0x100Cu);
jojo::step_r3000a(s,bus);
CHECK(s.gpr[2]==11u && s.pc==0x100Cu);
```

Cover taken/not-taken BEQ/BNE/BLEZ/BGTZ/BLTZ/BGEZ, J/JAL/JR/JALR, JAL/JALR link=`branch_pc+8`, JALR `rd==rs` pre-write target, and BLTZAL/BGEZAL unconditional link. A control transfer inside active delay slot must return `unpredictable_delay_slot_control_transfer`.

Extend exception tests: delay-slot fault => `Cause.BD=1`, `EPC=branch_pc`; when transfer was taken/unconditional, `Cause.BT=1` and TAR equals target.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_control_flow_tests jojo_r3000a_exception_tests -j2
ctest --test-dir build -R "jojo_r3000a_(control_flow|exception)_tests" --output-on-failure
```

- [ ] **Step 3: Implement delay-slot state**

At step entry:

```cpp
const auto current_delay=s.delay_slot;
const bool in_delay_slot=current_delay.active;
```

A new branch outside a delay slot stores:

```cpp
s.delay_slot={true,instruction_pc,taken,branch_target};
s.pc=instruction_pc+4u;
s.next_pc=taken?branch_target:instruction_pc+8u;
```

After the delay-slot instruction retires, clear `delay_slot`. Exception entry reads `current_delay` to set BD/BT/EPC/TAR.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(control_flow|exception|integer)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_control_flow.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: model R3000A delay slots"
```

---

### Task 6: Aligned Memory and One-Instruction Load Delay

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_memory.cpp`
- Modify: `tests/r3000a_test_bus.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write RED load-delay and alignment tests**

```cpp
s.gpr[4]=0x2000u; s.gpr[8]=0x11111111u;
bus.store32(0x2000u,0xAABBCCDDu);
bus.store32(0x1000u,test_mips::i(0x23,4,8,0));
bus.store32(0x1004u,test_mips::r(8,0,9,0,0x21));
bus.store32(0x1008u,test_mips::r(8,0,10,0,0x21));
jojo::step_r3000a(s,bus); jojo::step_r3000a(s,bus);
CHECK(s.gpr[9]==0x11111111u);
jojo::step_r3000a(s,bus); CHECK(s.gpr[10]==0xAABBCCDDu);
```

Add same-destination overwrite (`LW $t0` then `ADDIU $t0,$zero,7` => 7), load-to-zero access/fault behavior, LB/LBU/LH/LHU/LW/SB/SH/SW little-endian tests, alignment AdEL/AdES, data bus error -> DBE, unsupported address -> boundary, and pending-load retirement before a following exception handler begins.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_memory_tests -j2
ctest --test-dir build -R jojo_r3000a_memory_tests --output-on-failure
```

- [ ] **Step 3: Implement retirement order**

Capture sources before retiring the previous pending load:

```cpp
const auto old_pending=s.pending_load;
// capture rs/rt and compute current semantics
if(old_pending.valid && old_pending.reg!=0u) s.gpr[old_pending.reg]=old_pending.value;
if(direct_write.valid && direct_write.reg!=0u) s.gpr[direct_write.reg]=direct_write.value;
s.pending_load=new_pending;
s.gpr[0]=0u;
```

A faulting current load schedules no new pending result. Alignment is checked before bus access. Unsupported bus status remains a non-architectural boundary.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(memory|exception|integer)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/r3000a_test_bus.h tests/test_r3000a_memory.cpp
git commit -m "feat: add R3000A memory load delay"
```

---

### Task 7: LWL/LWR/SWL/SWR Merge Semantics

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_unaligned.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write exact RED lane tables**

For memory word `0x44332211` and GPR `0xAABBCCDD`:

```cpp
const std::uint32_t lwl_expected[4]={0x11BBCCDDu,0x2211CCDDu,0x332211DDu,0x44332211u};
const std::uint32_t lwr_expected[4]={0x44332211u,0xAA443322u,0xAABB4433u,0xAABBCC44u};
const std::uint32_t swl_expected[4]={0x443322AAu,0x4433AABBu,0x44AABBCCu,0xAABBCCDDu};
const std::uint32_t swr_expected[4]={0xAABBCCDDu,0xBBCCDD11u,0xCCDD2211u,0xDD332211u};
```

Test all low address bits. Test back-to-back LWL/LWR to same `rt` and verify second merge uses the pending value while an ordinary ALU consumer still sees the pre-load visible GPR until retirement.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_r3000a_unaligned_tests -j2
ctest --test-dir build -R jojo_r3000a_unaligned_tests --output-on-failure
```

- [ ] **Step 3: Implement explicit byte-lane switches**

Use `aligned=effective & ~3u` and `lane=effective & 3u`. Do not use host-endian reinterpret casts. For LWL/LWR:

```cpp
std::uint32_t merge_base=captured_rt;
if(s.pending_load.valid && s.pending_load.reg==ins.rt) merge_base=s.pending_load.value;
```

The merged load becomes `new_pending`. SWL/SWR read the aligned backing word, merge exact lanes, and call `write32` once.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(unaligned|memory)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_unaligned.cpp
git commit -m "feat: add R3000A unaligned word merges"
```

---

### Task 8: COP0, RFE and Interrupt Admission

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_cop0.cpp`
- Modify: `tests/test_r3000a_exceptions.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Supported readable COP0 registers in M2: TAR r6, BadVAddr r8, Status r12, Cause r13, EPC r14.
- Writable behavior: Status r12 masked; Cause r13 only bits8-9; TAR/BadVAddr/EPC remain read-only and MTC0 attempts leave them unchanged while returning `architectural_operation_unimplemented` with `register_index`.

- [ ] **Step 1: Write RED COP0 tests**

Production constants:

```cpp
constexpr std::uint32_t kStatusWritableMask=0xF27FFF3Fu;
constexpr std::uint32_t kCauseSoftwareInterruptMask=0x00000300u;
constexpr std::uint32_t kCauseExternalInterruptMask=0x0000FC00u;
constexpr std::uint32_t kCauseBd=1u<<31;
constexpr std::uint32_t kCauseBt=1u<<30;
```

Test MFC0 one-instruction load delay. Test MTC0 Status preserves bits outside the mask. Test MTC0 Cause changes only bits8-9. Test MTC0 to r6/r8/r14 returns `architectural_operation_unimplemented` and leaves the register unchanged. Reading unavailable r0/r1/r2/r4/r10 raises RI. RFE must restore low stack bits and never jump to EPC:

```cpp
const auto low=s.cop0.status & 0x3fu;
const auto expected=(low & 0x30u)|((low>>2)&0x0fu);
```

- [ ] **Step 2: Write RED interrupt tests**

`external_interrupt_pending` uses only bits2..7. Synchronize them as:

```cpp
s.cop0.cause=(s.cop0.cause & ~0x0000FC00u)|
             (std::uint32_t(s.external_interrupt_pending & 0xFCu)<<8);
```

Set IEc and a matching Status.IM bit; prove interrupt entry occurs before ordinary fetch when no delay slot is pending. Disable IEc or the mask and prove normal execution. Issue a branch, then raise an interrupt before the delay slot: delay slot must retire first. A pending GPR load must retire before accepted interrupt entry.

- [ ] **Step 3: Run RED**

```bash
cmake --build build --target jojo_r3000a_cop0_tests -j2
ctest --test-dir build -R "jojo_r3000a_(cop0|exception)_tests" --output-on-failure
```

- [ ] **Step 4: Implement COP0/RFE/interrupt logic**

MFC0 uses the same delayed GPR path as memory loads. RFE:

```cpp
const auto low=s.cop0.status & 0x3fu;
s.cop0.status=(s.cop0.status & ~0x3fu)|(low & 0x30u)|((low>>2)&0x0fu);
```

Interrupt acceptance predicate:

```cpp
const bool iec=(s.cop0.status & 1u)!=0u;
const bool pending=(s.cop0.cause & s.cop0.status & 0x0000FF00u)!=0u;
if(iec && pending && !s.delay_slot.active) { /* retire old load, enter INT */ }
```

- [ ] **Step 5: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(cop0|control_flow|memory|exception)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_cop0.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: add R3000A COP0 interrupts"
```

---

### Task 9: COP2 Boundary and PS-X EXE Initialization

**Files:**
- Modify: `src/core/r3000a_reference_executor.h/.cpp`
- Create: `tests/test_r3000a_boundary.cpp`
- Create: `tests/test_r3000a_init.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: COP2 decoded operations + existing `Ps1ExeMetadata`.
- Produces: CpU vs `cop2_unimplemented` and `R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata&) noexcept`.

- [ ] **Step 1: Write RED COP2 tests**

With Status.CU2 bit30 clear, MFC2/CFC2/MTC2/CTC2/COP2 command must raise CpU with Cause.CE=2 and diagnostic coprocessor=2. With CU2 set, all remain explicit `cop2_unimplemented` boundaries; none retire as NOP.

- [ ] **Step 2: Write RED initializer tests**

```cpp
jojo::Ps1ExeMetadata m{};
m.entry_pc=0x80010000u; m.initial_gp=0x80018000u;
m.stack_base=0x801FFF00u; m.stack_size=0x100u;
auto s=jojo::initialize_r3000a_for_psx_exe(m);
CHECK(s.pc==0x80010000u && s.next_pc==0x80010004u);
CHECK(s.gpr[28]==0x80018000u && s.gpr[29]==0x80200000u);
CHECK(s.gpr[0]==0u && !s.pending_load.valid && !s.delay_slot.active);
```

Zero stack fields leave SP zero. HI/LO/COP0/delay state initialize to zero.

- [ ] **Step 3: Run RED**

```bash
cmake --build build --target jojo_r3000a_boundary_tests jojo_r3000a_init_tests -j2
ctest --test-dir build -R "jojo_r3000a_(boundary|init)_tests" --output-on-failure
```

- [ ] **Step 4: Implement exact public API**

```cpp
[[nodiscard]] R3000aStepResult step_r3000a(R3000aState&,R3000aBus&) noexcept;
[[nodiscard]] R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata&) noexcept;
```

Initializer sets `pc`, `next_pc`, `$gp` (r28), and if either stack field is non-zero sets `$sp` (r29) to `stack_base + stack_size` using defined uint32_t wrap. It does not load commercial bytes or invoke BIOS/HLE.

- [ ] **Step 5: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(boundary|init|cop0)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.* tests/test_r3000a_boundary.cpp tests/test_r3000a_init.cpp
git commit -m "feat: add R3000A COP2 boundary init"
```

---

### Task 10: Determinism, Full CI and Truthful R2.3 Status

**Files:**
- Modify: `tests/test_r3000a_init.cpp`
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`
- Verify: `cmake/CheckPs1ActiveArchitecture.cmake`
- Verify: `.github/workflows/build.yml`

- [ ] **Step 1: Add deterministic replay contract**

Execute one synthetic sequence twice from cloned CPU/bus state. Sequence must include delayed load, ALU consumer, branch+delay slot, HI/LO and non-faulting COP0 access. Compare all architectural fields and step statuses:

```cpp
CHECK(a.gpr==b.gpr); CHECK(a.hi==b.hi && a.lo==b.lo);
CHECK(a.pc==b.pc && a.next_pc==b.next_pc);
CHECK(a.cop0.status==b.cop0.status && a.cop0.cause==b.cop0.cause);
CHECK(a.cop0.epc==b.cop0.epc && a.cop0.bad_vaddr==b.cop0.bad_vaddr);
CHECK(a.cop0.target_address==b.cop0.target_address);
CHECK(a.pending_load.valid==b.pending_load.valid && a.delay_slot.active==b.delay_slot.active);
```

- [ ] **Step 2: Run complete local verification**

```bash
cmake -S . -B build
cmake --build build -j2
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
```

Expected: zero failures and architecture gate PASS.

- [ ] **Step 3: Push code head and require Linux + Windows CI**

Require the workflow tied to the exact code SHA to report:

```text
Portable core / Linux: success
Windows x64 / MSVC 2022: success
PS1 active architecture gate: success
CTest: success
```

If any check fails, use systematic-debugging and do not change readiness documents.

- [ ] **Step 4: Update the existing R2.3 row using that successful run as evidence**

The current table already contains R2.3. Replace only that row with:

```text
R2.3	implemented-unverified	github-actions:run-<successful-code-run-id>	ps1-memory-bus-bios-hle-not-implemented
```

Do not add a parallel M2 row. Keep R2.4 `not-started`; no boot/render/audio/input/gameplay state becomes verified.

Update `PROJECT-STATE.md` with the same code run ID and this exact truth boundary:

```text
R3000A reference CPU semantics are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, rendering, audio, input and gameplay are not verified.
```

Update `docs/NEXT-MILESTONES.md` so the next architectural cycle is JoJo-specific PS1 memory/bus + BIOS/HLE; CFG/IR/x64 stays downstream of the reference executor.

- [ ] **Step 5: Commit documentation evidence**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv tests/test_r3000a_init.cpp
git commit -m "docs: verify R3000A reference core M2"
```

- [ ] **Step 6: Push the documentation head and require final Linux + Windows CI again**

The final exact head must pass build, CTest and PS1 architecture gate on both platforms. The prior code run remains the evidence value in R2.3 because it is the run that verified the code before the documentation-only evidence commit.

- [ ] **Step 7: Final checklist before completion claim**

```text
MIPS decoder green
integer/$zero/immediate green
precise exception/overflow/RI/bus-fault green
HI/LO edge cases green
branch/jump/delay-slot green
aligned memory/load-delay green
LWL/LWR/SWL/SWR green
COP0/RFE/interrupt green
COP2 boundary green
PS-X EXE initializer green
deterministic replay green
PS1 architecture gate green
Linux final CI green
Windows final CI green
R2.3 only implemented-unverified
no boot/render/audio/input/gameplay promotion
```

---

## Completion Boundary

M2 is complete only when every task above is implemented and the exact final head is green on Linux and Windows. `reference-execution-ready` means the R3000A reference CPU semantic contract is ready to serve as an oracle; it does not mean the JoJo commercial executable boots. The next independent design/spec cycle is JoJo-specific PS1 memory/bus + BIOS/HLE.