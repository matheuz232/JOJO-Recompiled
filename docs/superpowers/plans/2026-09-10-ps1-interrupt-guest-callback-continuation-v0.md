# PS1 Interrupt Guest Callback Continuation v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace zero-filled exception-vector fallthrough with a deterministic HLE interrupt dispatcher that executes registered IntRP callbacks as ordinary guest R3000A code and returns through `B0:17`/HookEntryInt without expanding unrelated BIOS, CD-ROM, GPU, DMA, or event behavior.

**Architecture:** Add a value-type `Ps1InterruptContinuation` state machine owned by `Ps1BootRuntime`. The continuation captures the interrupted context after architectural interrupt entry, walks the existing `Ps1HleBios` priority-chain metadata, launches guest FIRST/SECOND callbacks through the ordinary CPU loop using an internal unmapped return sentinel, handles `B0:17 ReturnFromException`, and optionally resumes a guest HookEntryInt buffer. `Ps1BootRuntime` intercepts only an all-zero normal exception vector at `0x80000080`; custom vectors remain guest-owned.

**Tech Stack:** C++20, CMake 3.20+, portable R3000A reference executor, `Ps1MemoryBus`, `Ps1HleBios`, MAX3 explorer, GitHub Actions Linux + Windows Server 2022 / Visual Studio 2022 x64.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-interrupt-guest-callback-continuation-v0-design.md`

## Global Constraints

- Design base is `f485a6fa3434f05505205589a5aa181f055d3308`; the written spec head is `4ce652af118389d9c65dc45bc5c520359461d3a0`.
- Do not import, materialize, execute, commit, artifact, or release a retail PS1 BIOS image or proprietary game payload.
- Use only synthetic fixtures in repository tests; commercial checkpoints remain external evidence only.
- Intercept only the normal exception vector `0x80000080`, and only when all four words at `0x80000080..0x8000008C` are readable and zero.
- Any non-zero word in that vector preserves ordinary guest execution; BEV/ROM-vector handling remains out of scope.
- FIRST/SECOND callbacks execute through the ordinary `step_r3000a` path and may use only already-supported BIOS/MMIO behavior.
- `B0:17 ReturnFromException` is valid only while an interrupt continuation is active; outside that state it remains a strict BIOS frontier.
- Do not auto-clear I_STAT, CD-ROM HINTSTS, or any device IRQ merely to avoid re-entry.
- Do not implement `A0:35 lsearch`, new CD-ROM commands/FIFO semantics, `B0:07 DeliverEvent`, general event callbacks, DMA3, GPU drawing, linked-list DMA2, renderer/presentation, nested kernel exceptions, or native x64 lowering in this milestone.
- Preserve MAX3 copyability and deterministic hashing: continuation state must contain no host coroutine, opaque pointer, or host-stack continuation.
- Every production change follows TDD RED → GREEN; Windows/MSVC CI remains the final cross-platform authority.

---

## File Structure

**Create:**
- `src/core/ps1_interrupt_continuation.h` — public value-state interface, phases, saved interrupted context, drive/restore API, internal return sentinel.
- `src/core/ps1_interrupt_continuation.cpp` — priority traversal, callback launch/return, cycle guard, HookEntryInt restore, deterministic hash.
- `tests/test_ps1_interrupt_continuation.cpp` — focused unit tests for saved context, chain ordering, FIRST/SECOND decisions, callback sentinel, hook restore, cycle strictness, hash.

**Modify:**
- `src/core/ps1_hle_bios.h` — expose priority-head query and an explicit `return_from_exception` disposition.
- `src/core/ps1_hle_bios.cpp` — recognize `B0:17` without mutating CPU state; expose the current IntRP head.
- `src/core/ps1_boot_runtime.h` — own one `Ps1InterruptContinuation` value.
- `src/core/ps1_boot_runtime.cpp` — all-zero vector probe, pre-step interrupt snapshot, continuation driving, nested-interrupt boundary, `B0:17` consumption, continuation hash.
- `tests/test_ps1_kernel_hle_frontier.cpp` — BIOS metadata/control-transfer contract for C0 heads and B0:17.
- `tests/test_ps1_cdrom_boot_runtime.cpp` — accepted-CDROM-IRQ integration regression, zero-vector interception, custom-vector preservation, context restore.
- `tests/test_ps1_max3_explorer.cpp` — prove fake `A0:35` no longer becomes a MAX3 dependency and no unsupported CD-ROM relaxation was introduced.
- `CMakeLists.txt` — compile the continuation source and register its focused test executable.

No public checkpoint-format change is planned.

---

### Task 1: Expose BIOS interrupt metadata and `B0:17` control transfer

**Files:**
- Modify: `src/core/ps1_hle_bios.h`
- Modify: `src/core/ps1_hle_bios.cpp`
- Test: `tests/test_ps1_kernel_hle_frontier.cpp`

**Interfaces:**
- Consumes: existing `Ps1HleBios::dispatch(...)`, `interrupt_priority_heads_`, `B0:19 HookEntryInt`, `C0:02/C0:03` storage.
- Produces:
  - `Ps1HleBiosDisposition::return_from_exception`
  - `std::optional<std::uint32_t> Ps1HleBios::interrupt_priority_head(std::uint32_t priority) const noexcept`

- [ ] **Step 1: Write RED tests for priority-head visibility and B0:17 signaling**

Extend `tests/test_ps1_kernel_hle_frontier.cpp` with a test that enqueues one synthetic node, reads the head through the new query, then dispatches B0:17 and proves the CPU is not mutated by HLE:

```cpp
static void test_interrupt_metadata_and_return_from_exception_signal() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};

    cpu.pc = 0x000000C0u;
    cpu.next_pc = 0x000000C4u;
    cpu.gpr[4] = 2u;
    cpu.gpr[5] = 0x80001000u;
    cpu.gpr[31] = 0x80014000u;
    CHECK(bios.dispatch(call(jojo::Ps1HleBiosDomain::c0, 0x02u, cpu), cpu, bus).disposition ==
          jojo::Ps1HleBiosDisposition::handled);
    CHECK(bios.interrupt_priority_head(2u).value_or(0u) == 0x80001000u);
    CHECK(!bios.interrupt_priority_head(4u).has_value());

    cpu.pc = 0x000000B0u;
    cpu.next_pc = 0x000000B4u;
    cpu.gpr[2] = 0x12345678u;
    cpu.gpr[31] = 0x80015000u;
    const auto before = cpu;
    const auto result = bios.dispatch(call(jojo::Ps1HleBiosDomain::b0, 0x17u, cpu), cpu, bus);
    CHECK(result.disposition == jojo::Ps1HleBiosDisposition::return_from_exception);
    CHECK(cpu.pc == before.pc);
    CHECK(cpu.next_pc == before.next_pc);
    CHECK(cpu.gpr[2] == before.gpr[2]);
    CHECK(cpu.gpr[31] == before.gpr[31]);
}
```

Add the test to `main()`.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target jojo_ps1_kernel_hle_frontier_tests
ctest --test-dir build -R jojo_ps1_kernel_hle_frontier_tests --output-on-failure
```

Expected RED: compilation fails because `return_from_exception` and/or `interrupt_priority_head()` do not exist.

- [ ] **Step 3: Add the minimal BIOS interfaces**

In `src/core/ps1_hle_bios.h`, extend the disposition and add the query:

```cpp
enum class Ps1HleBiosDisposition : std::uint8_t {
    handled,
    unsupported,
    terminal,
    return_from_exception,
};

class Ps1HleBios {
public:
    // existing API...
    [[nodiscard]] std::optional<std::uint32_t> interrupt_priority_head(
        std::uint32_t priority) const noexcept;
};
```

In the B0 switch in `src/core/ps1_hle_bios.cpp`, signal B0:17 without changing PC, registers, bus state, or the saved interrupt metadata:

```cpp
case 0x17u: // ReturnFromException: Ps1BootRuntime owns context restoration.
    return {Ps1HleBiosDisposition::return_from_exception};
```

Add the query implementation:

```cpp
std::optional<std::uint32_t> Ps1HleBios::interrupt_priority_head(
    std::uint32_t priority) const noexcept {
    if (priority >= interrupt_priority_heads_.size()) return std::nullopt;
    return interrupt_priority_heads_[static_cast<std::size_t>(priority)];
}
```

Do not change `C0:02`, `C0:03`, `B0:18`, or `B0:19` semantics.

- [ ] **Step 4: Run focused and neighboring BIOS tests and verify GREEN**

Run:

```bash
cmake --build build --parallel 2 --target jojo_ps1_kernel_hle_frontier_tests jojo_ps1_hle_bios_tests
ctest --test-dir build -R 'jojo_ps1_(kernel_hle_frontier|hle_bios)_tests' --output-on-failure
```

Expected: both targets pass.

- [ ] **Step 5: Commit Task 1**

```bash
git add src/core/ps1_hle_bios.h src/core/ps1_hle_bios.cpp tests/test_ps1_kernel_hle_frontier.cpp
git commit -m "feat: expose PS1 interrupt HLE control flow"
```

---

### Task 2: Add deterministic interrupted-context state and restoration

**Files:**
- Create: `src/core/ps1_interrupt_continuation.h`
- Create: `src/core/ps1_interrupt_continuation.cpp`
- Create: `tests/test_ps1_interrupt_continuation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R3000aState` and existing FNV-style deterministic hash conventions.
- Produces:

```cpp
enum class Ps1InterruptContinuationPhase : std::uint8_t {
    inactive,
    dispatch,
    first_callback,
    second_callback,
    hook_guest,
};

struct Ps1InterruptedContext {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t status{};
    std::uint32_t resume_pc{};
    std::uint32_t resume_next_pc{};
};

class Ps1InterruptContinuation {
public:
    static constexpr std::uint32_t callback_return_sentinel = 0xE00000F0u;

    void begin(const R3000aState& post_exception,
               std::uint32_t pre_exception_status,
               std::uint32_t resume_pc,
               std::uint32_t resume_next_pc) noexcept;
    void return_from_exception(R3000aState& cpu) noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] Ps1InterruptContinuationPhase phase() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
};
```

The sentinel is intentionally `>= 0xC0000000`, so `Ps1MemoryBus::guest_to_physical()` does not map it.

- [ ] **Step 1: Register the new source/test and write RED context tests**

Add `src/core/ps1_interrupt_continuation.cpp` to `jojo_core` and:

```cmake
add_jojo_test(jojo_ps1_interrupt_continuation_tests tests/test_ps1_interrupt_continuation.cpp)
```

Create `tests/test_ps1_interrupt_continuation.cpp` with tests that prove exact capture/restoration and hash behavior:

```cpp
#include "core/ps1_interrupt_continuation.h"
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ \
    << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void test_begin_and_restore_exact_v0_context() {
    jojo::R3000aState cpu{};
    for (std::uint32_t i = 1; i < 32; ++i) cpu.gpr[i] = 0x10000000u + i;
    cpu.hi = 0x11112222u;
    cpu.lo = 0x33334444u;
    cpu.cop0.status = 0x00000404u; // post-exception stack value; not restored as-is
    cpu.cop0.cause = 0x00000400u;
    cpu.cop0.epc = 0x80012000u;
    cpu.cop2_gte.control[0] = 0xABCDEF01u;
    cpu.external_interrupt_pending = 0x04u;

    jojo::Ps1InterruptContinuation continuation;
    const auto inactive_hash = continuation.diagnostic_state_hash();
    continuation.begin(cpu, 0x00000401u, 0x80012000u, 0x80012004u);
    CHECK(continuation.active());
    CHECK(continuation.phase() == jojo::Ps1InterruptContinuationPhase::dispatch);
    CHECK(continuation.diagnostic_state_hash() != inactive_hash);

    cpu.gpr[5] = 0xDEADBEEFu;
    cpu.hi = 1u;
    cpu.lo = 2u;
    cpu.cop0.status = 0u;
    cpu.cop0.cause = 0x12340400u;
    cpu.cop0.epc = 0x87654321u;
    cpu.cop2_gte.control[0] = 0x10203040u;
    cpu.external_interrupt_pending = 0x08u;
    cpu.pending_load = {true, 7u, 0xCAFEBABEu};
    cpu.delay_slot.active = true;

    continuation.return_from_exception(cpu);
    CHECK(!continuation.active());
    CHECK(cpu.gpr[5] == 0x10000005u);
    CHECK(cpu.hi == 0x11112222u && cpu.lo == 0x33334444u);
    CHECK(cpu.cop0.status == 0x00000401u);
    CHECK(cpu.pc == 0x80012000u && cpu.next_pc == 0x80012004u);
    CHECK(!cpu.pending_load.valid && !cpu.delay_slot.active);
    CHECK(cpu.cop0.cause == 0x12340400u);
    CHECK(cpu.cop0.epc == 0x87654321u);
    CHECK(cpu.cop2_gte.control[0] == 0x10203040u);
    CHECK(cpu.external_interrupt_pending == 0x08u);
}

static void test_equal_continuations_hash_equal() {
    jojo::R3000aState cpu{};
    cpu.gpr[3] = 0x1234u;
    jojo::Ps1InterruptContinuation a;
    jojo::Ps1InterruptContinuation b;
    a.begin(cpu, 0x401u, 0x80010000u, 0x80010004u);
    b.begin(cpu, 0x401u, 0x80010000u, 0x80010004u);
    CHECK(a.diagnostic_state_hash() == b.diagnostic_state_hash());
}

int main() {
    test_begin_and_restore_exact_v0_context();
    test_equal_continuations_hash_equal();
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Run the new test and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target jojo_ps1_interrupt_continuation_tests
```

Expected RED: missing header/source/types.

- [ ] **Step 3: Implement value-state capture, restore, and deterministic hash**

Create the header with the exact public API above. Private state must remain plain value data:

```cpp
Ps1InterruptContinuationPhase phase_{Ps1InterruptContinuationPhase::inactive};
Ps1InterruptedContext saved_{};
std::uint8_t priority_{};
std::uint32_t current_node_{};
std::uint32_t next_node_{};
std::uint32_t first_callback_{};
std::uint32_t second_callback_{};
bool priority_head_loaded_{};
std::array<std::uint32_t, 64> visited_nodes_{};
std::size_t visited_count_{};
```

`begin()` copies GPR1..31, HI/LO from the post-exception state, stores the passed pre-exception Status and resume PCs, clears traversal fields, and sets `phase_ = dispatch`. `return_from_exception()` restores only the fields defined by the spec:

```cpp
for (std::size_t i = 1; i < saved_.gpr.size(); ++i) cpu.gpr[i] = saved_.gpr[i];
cpu.hi = saved_.hi;
cpu.lo = saved_.lo;
cpu.cop0.status = saved_.status;
cpu.pc = saved_.resume_pc;
cpu.next_pc = saved_.resume_next_pc;
cpu.pending_load = {};
cpu.delay_slot = {};
cpu.gpr[0] = 0u;
phase_ = Ps1InterruptContinuationPhase::inactive;
```

Do not restore Cause, EPC, COP2/GTE, or `external_interrupt_pending`.

Hash every field that can affect future traversal/restoration, including all saved GPRs, traversal fields, phase, and the populated visited-node prefix.

- [ ] **Step 4: Run the focused test and verify GREEN**

```bash
cmake --build build --parallel 2 --target jojo_ps1_interrupt_continuation_tests
ctest --test-dir build -R jojo_ps1_interrupt_continuation_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 2**

```bash
git add CMakeLists.txt src/core/ps1_interrupt_continuation.h src/core/ps1_interrupt_continuation.cpp tests/test_ps1_interrupt_continuation.cpp
git commit -m "feat: add PS1 interrupt continuation state"
```

---

### Task 3: Walk IntRP chains and execute FIRST/SECOND through a guest-return sentinel

**Files:**
- Modify: `src/core/ps1_interrupt_continuation.h`
- Modify: `src/core/ps1_interrupt_continuation.cpp`
- Modify/Test: `tests/test_ps1_interrupt_continuation.cpp`

**Interfaces:**
- Consumes: Task 1 `interrupt_priority_head()`, Task 2 continuation state, `Ps1MemoryBus::read32()`.
- Produces:

```cpp
enum class Ps1InterruptDriveStatus : std::uint8_t {
    guest_execution,
    restored,
    terminal,
};

struct Ps1InterruptDriveResult {
    Ps1InterruptDriveStatus status{Ps1InterruptDriveStatus::terminal};
};

[[nodiscard]] Ps1InterruptDriveResult drive(
    R3000aState& cpu,
    Ps1MemoryBus& bus,
    const Ps1HleBios& bios) noexcept;
```

- [ ] **Step 1: Write RED tests for chain order, FIRST/SECOND rules, and sentinel return**

Extend `tests/test_ps1_interrupt_continuation.cpp` with helpers that materialize IntRP nodes through the bus and register heads through `C0:02`. Use direct sentinel simulation for the unit test: after `drive()` launches a callback, set `cpu.pc = callback_return_sentinel` and set `cpu.gpr[2]` to the desired return value before calling `drive()` again.

Cover these exact cases:

```cpp
// priority 0 node: next=0, second=0x80012100, first=0x80012000
// FIRST returns 0 -> SECOND is skipped and dispatch completes/restores.
CHECK(continuation.drive(cpu, bus, bios).status == jojo::Ps1InterruptDriveStatus::guest_execution);
CHECK(cpu.pc == 0x80012000u);
CHECK(cpu.gpr[31] == jojo::Ps1InterruptContinuation::callback_return_sentinel);
cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
cpu.gpr[2] = 0u;
CHECK(continuation.drive(cpu, bus, bios).status == jojo::Ps1InterruptDriveStatus::restored);

// FIRST returns 1 -> SECOND launches.
// after SECOND reaches sentinel, next linked node launches before next priority.
```

Add a multi-priority test with synthetic callback addresses encoding order (for example `0x80012000`, `0x80012100`, `0x80012200`, `0x80012300`) and check launch order is linked-list order inside a priority, then priority 0→1→2→3.

Add cycle strictness:

```cpp
// node.next points to itself
CHECK(continuation.drive(cpu, bus, bios).status == jojo::Ps1InterruptDriveStatus::guest_execution);
cpu.pc = jojo::Ps1InterruptContinuation::callback_return_sentinel;
cpu.gpr[2] = 0u;
CHECK(continuation.drive(cpu, bus, bios).status == jojo::Ps1InterruptDriveStatus::terminal);
```

- [ ] **Step 2: Run focused test and verify RED**

```bash
cmake --build build --parallel 2 --target jojo_ps1_interrupt_continuation_tests
ctest --test-dir build -R jojo_ps1_interrupt_continuation_tests --output-on-failure
```

Expected RED: `drive`, drive status types, or traversal behavior are missing.

- [ ] **Step 3: Implement traversal and callback launch**

Add `drive()` and these private helpers:

```cpp
[[nodiscard]] bool remember_node(std::uint32_t node) noexcept;
void launch_callback(R3000aState& cpu,
                     std::uint32_t callback,
                     Ps1InterruptContinuationPhase phase) noexcept;
```

`remember_node()` rejects zero, repeated addresses, and a 65th distinct node. Do not silently skip malformed chains.

`launch_callback()` must do exactly:

```cpp
cpu.pc = callback;
cpu.next_pc = callback + 4u;
cpu.delay_slot = {};
cpu.pending_load = {};
cpu.gpr[31] = callback_return_sentinel;
cpu.gpr[0] = 0u;
phase_ = phase;
```

`drive()` behavior:

```cpp
if (phase_ == Ps1InterruptContinuationPhase::first_callback) {
    if (cpu.pc != callback_return_sentinel)
        return {Ps1InterruptDriveStatus::guest_execution};
    const bool first_nonzero = cpu.gpr[2] != 0u;
    if (first_nonzero && second_callback_ != 0u) {
        launch_callback(cpu, second_callback_, Ps1InterruptContinuationPhase::second_callback);
        return {Ps1InterruptDriveStatus::guest_execution};
    }
    current_node_ = next_node_;
    phase_ = Ps1InterruptContinuationPhase::dispatch;
}

if (phase_ == Ps1InterruptContinuationPhase::second_callback) {
    if (cpu.pc != callback_return_sentinel)
        return {Ps1InterruptDriveStatus::guest_execution};
    current_node_ = next_node_;
    phase_ = Ps1InterruptContinuationPhase::dispatch;
}
```

For `dispatch`, loop until a guest callback is launched or dispatch completes:

1. Load the current priority head once with `bios.interrupt_priority_head(priority_)`.
2. If the current chain is empty, advance priority.
3. For each node, reject a repeated/overflow node through `terminal`.
4. Read `next`, `SECOND`, `FIRST` from `node+0`, `node+4`, `node+8`; any unsupported read returns `terminal` and leaves the bus's unsupported-access evidence intact.
5. Capture all three pointers before executing a callback.
6. If FIRST is zero, skip the node's SECOND and advance directly to captured `next`.
7. If FIRST is non-zero, launch it and return `guest_execution`.
8. When priority 3 is exhausted and no HookEntryInt handling exists yet, call `return_from_exception(cpu)` and return `restored`. Task 5 will replace this final branch with hook-aware completion.

Update the continuation hash to include all new traversal fields and visited-node state.

- [ ] **Step 4: Run focused tests and verify GREEN**

```bash
cmake --build build --parallel 2 --target jojo_ps1_interrupt_continuation_tests jojo_ps1_kernel_hle_frontier_tests
ctest --test-dir build -R 'jojo_ps1_(interrupt_continuation|kernel_hle_frontier)_tests' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 3**

```bash
git add src/core/ps1_interrupt_continuation.h src/core/ps1_interrupt_continuation.cpp tests/test_ps1_interrupt_continuation.cpp
git commit -m "feat: dispatch PS1 IntRP guest callbacks"
```

---

### Task 4: Integrate all-zero exception-vector interception into `Ps1BootRuntime`

**Files:**
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Test: `tests/test_ps1_cdrom_boot_runtime.cpp`

**Interfaces:**
- Consumes: Task 2/3 `Ps1InterruptContinuation`, Task 1 `return_from_exception`, existing `step_r3000a`, I_STAT/I_MASK→IP2 sync.
- Produces: one copyable `Ps1InterruptContinuation interrupt_continuation_{}` member included in runtime diagnostic hashing.

- [ ] **Step 1: Write RED integration tests for zero vector, custom vector, B0:17, and nested interrupt strictness**

Add a helper in `tests/test_ps1_cdrom_boot_runtime.cpp` that constructs a program which enables I_MASK bit 2, enables COP0 IEc+IM2, emits CD-ROM command `0x01`, and leaves `r9=0x35` before the interrupt. Do not materialize a vector.

Add a zero-vector regression:

```cpp
jojo::Ps1BootOptions options{};
options.instruction_budget = 128u;
options.bios_event_capacity = 16u;
const auto report = runtime.run(options);
CHECK(report.interrupts_accepted == 1u);
for (const auto& event : report.recent_bios_calls) {
    CHECK(!(event.table == 0xA0u && event.selector == 0x35u));
}
```

Register a synthetic priority-2 node whose FIRST callback is:

```cpp
// at 0x80012000: B0:17 ReturnFromException
addiu r9, r0, 0x17
j     0x000000B0
nop
```

After the run, assert execution returns to the interrupted program and does not fall through to `0x800000A0`.

Add custom-vector preservation by writing a non-zero self-loop at `0x80000080` before the IRQ and asserting the continuation does not hijack it: the run exhausts budget while PC remains in the custom vector and no registered IntRP callback executes.

Add nested-interrupt strictness with a FIRST callback that re-enables interrupt acceptance while the original I_STAT remains asserted. Expected stop: `cpu_boundary` with the second step diagnostic exception code `interrupt`; continuation state must not be overwritten by a second `begin()`.

- [ ] **Step 2: Run the focused runtime test and verify RED**

```bash
cmake --build build --parallel 2 --target jojo_ps1_cdrom_boot_runtime_tests
ctest --test-dir build -R jojo_ps1_cdrom_boot_runtime_tests --output-on-failure
```

Expected RED: zero vector still falls through to fake A0 and runtime has no continuation integration.

- [ ] **Step 3: Add vector probe and continuation member**

In `src/core/ps1_boot_runtime.h` include the new header and add:

```cpp
Ps1InterruptContinuation interrupt_continuation_{};
```

In `src/core/ps1_boot_runtime.cpp`, add:

```cpp
std::optional<bool> zero_default_exception_vector(Ps1MemoryBus& bus) noexcept {
    for (std::uint32_t address = 0x80000080u; address <= 0x8000008Cu; address += 4u) {
        const auto word = bus.read32(address);
        if (word.status != R3000aBusStatus::ok) return std::nullopt;
        if (word.value != 0u) return false;
    }
    return true;
}
```

At the beginning of each loop iteration, before BIOS-vector classification or bus fetch, drive an active continuation:

```cpp
if (interrupt_continuation_.active()) {
    const auto driven = interrupt_continuation_.drive(cpu_, bus_, hle_bios_);
    if (driven.status == Ps1InterruptDriveStatus::terminal) {
        report.unsupported_access = bus_.last_unsupported_access();
        return finish(Ps1BootStopReason::fatal_runtime_error);
    }
    if (driven.status == Ps1InterruptDriveStatus::restored) {
        continue;
    }
}
```

When BIOS dispatch returns `return_from_exception`, consume it only if a continuation is active:

```cpp
if (hle.disposition == Ps1HleBiosDisposition::return_from_exception) {
    if (!interrupt_continuation_.active()) {
        diagnostic_bios_frontier_pending_ = true;
        return finish(Ps1BootStopReason::bios_call_unimplemented);
    }
    interrupt_continuation_.return_from_exception(cpu_);
    diagnostic_bios_frontier_pending_ = false;
    continue;
}
```

This must occur before generic `handled/terminal/unsupported` processing.

- [ ] **Step 4: Capture pre-step interrupt state and intercept only the all-zero vector**

Immediately before `step_r3000a`, snapshot:

```cpp
const auto pre_step_status = cpu_.cop0.status;
const auto pre_step_pc = cpu_.pc;
const auto pre_step_next_pc = cpu_.next_pc;
```

On an interrupt exception:

```cpp
if (step.status == R3000aStepStatus::exception &&
    step.diagnostic.exception_code == R3000aExceptionCode::interrupt) {
    ++report.interrupts_accepted;
    instructions_since_progress = 0u;

    if (interrupt_continuation_.active()) {
        report.cpu_diagnostic = step.diagnostic;
        return finish(Ps1BootStopReason::cpu_boundary);
    }

    const auto zero_vector = zero_default_exception_vector(bus_);
    if (!zero_vector.has_value()) {
        report.unsupported_access = bus_.last_unsupported_access();
        return finish(Ps1BootStopReason::fatal_runtime_error);
    }
    if (*zero_vector && cpu_.pc == 0x80000080u) {
        interrupt_continuation_.begin(
            cpu_, pre_step_status, cpu_.cop0.epc, cpu_.cop0.epc + 4u);
    }
    continue;
}
```

Use `pre_step_pc`/`pre_step_next_pc` in tests or assertions to prove the accepted interrupt occurred outside a delay slot; do not synthesize a branch-delay resume mode.

Include the continuation hash in `Ps1BootRuntime::diagnostic_state_hash()`:

```cpp
hash_u64(hash, interrupt_continuation_.diagnostic_state_hash());
```

- [ ] **Step 5: Run runtime + CPU exception tests and verify GREEN**

```bash
cmake --build build --parallel 2 --target jojo_ps1_cdrom_boot_runtime_tests jojo_r3000a_cop0_tests jojo_r3000a_exception_tests
ctest --test-dir build -R 'jojo_(ps1_cdrom_boot_runtime|r3000a_(cop0|exception))_tests' --output-on-failure
```

Expected: all pass; custom vector remains guest-owned and zero vector no longer produces fake A0.

- [ ] **Step 6: Commit Task 4**

```bash
git add src/core/ps1_boot_runtime.h src/core/ps1_boot_runtime.cpp tests/test_ps1_cdrom_boot_runtime.cpp
git commit -m "feat: route zero PS1 exception vector through HLE"
```

---

### Task 5: Add HookEntryInt completion and full interrupt-return context tests

**Files:**
- Modify: `src/core/ps1_interrupt_continuation.h`
- Modify: `src/core/ps1_interrupt_continuation.cpp`
- Modify/Test: `tests/test_ps1_interrupt_continuation.cpp`
- Modify/Test: `tests/test_ps1_cdrom_boot_runtime.cpp`

**Interfaces:**
- Consumes: `Ps1HleBios::interrupt_hook_address()`, known ResetEntryInt default address `0x00006CF4`, Task 4 B0:17 runtime consumption.
- Produces: hook-aware completion in `drive()`; no new public checkpoint field.

- [ ] **Step 1: Write RED tests for no-hook/default-hook completion and guest HookEntryInt restore**

In the continuation unit test, cover direct restoration when no hook exists and when the hook is the known ResetEntryInt default `0x00006CF4`.

For a custom hook at `0x80003000`, write this exact 0x30-byte jmp buffer through `Ps1MemoryBus::write32`:

```cpp
bus.write32(0x80003000u + 0x00u, 0x80014000u); // resume PC / RA
bus.write32(0x80003000u + 0x04u, 0x801FF000u); // SP/R29
bus.write32(0x80003000u + 0x08u, 0x80003F00u); // FP/R30
for (std::uint32_t i = 0; i < 8; ++i)
    bus.write32(0x80003000u + 0x0Cu + i * 4u, 0x16000000u + i); // R16..R23
bus.write32(0x80003000u + 0x2Cu, 0x80004000u); // GP/R28
```

Set HookEntryInt through B0:19, exhaust empty priority chains, and assert `drive()` returns `guest_execution` with:

```cpp
CHECK(cpu.pc == 0x80014000u);
CHECK(cpu.next_pc == 0x80014004u);
CHECK(cpu.gpr[2] == 1u);
CHECK(cpu.gpr[29] == 0x801FF000u);
CHECK(cpu.gpr[30] == 0x80003F00u);
CHECK(cpu.gpr[28] == 0x80004000u);
CHECK(continuation.phase() == jojo::Ps1InterruptContinuationPhase::hook_guest);
```

Then call `return_from_exception(cpu)` and assert the original interrupted context, not the hook buffer, is restored.

- [ ] **Step 2: Add RED runtime tests for callback BIOS/MMIO and pending-load fidelity**

Add one synthetic FIRST callback that performs an already-supported B0 BIOS call, one supported MMIO write (for example I_STAT acknowledge or I_MASK write using existing semantics), then returns through `jr ra`. Confirm the callback's trace/BIOs/MMIO activity is visible and dispatch proceeds normally.

Add a pending-load case: arrange a load immediately before the point where IP2 becomes acceptable, verify the R3000A exception entry retires that load, then after B0:17 assert the loaded GPR value is preserved and `pending_load.valid == false`.

Add a hook integration callback at `0x80014000` that invokes B0:17 and prove execution returns to the original interrupted PC/context.

- [ ] **Step 3: Run focused tests and verify RED**

```bash
cmake --build build --parallel 2 --target jojo_ps1_interrupt_continuation_tests jojo_ps1_cdrom_boot_runtime_tests
ctest --test-dir build -R 'jojo_ps1_(interrupt_continuation|cdrom_boot_runtime)_tests' --output-on-failure
```

Expected RED: chain exhaustion currently restores directly and does not load custom HookEntryInt buffers.

- [ ] **Step 4: Implement hook-aware completion in `drive()`**

When priority 3 is exhausted:

```cpp
const auto hook = bios.interrupt_hook_address();
if (!hook || *hook == 0x00006CF4u) {
    return_from_exception(cpu);
    return {Ps1InterruptDriveStatus::restored};
}
```

For a custom hook, read all 12 words first. If any read fails, return `terminal` without partially mutating CPU. Only after all reads succeed, apply:

```cpp
cpu.gpr[31] = words[0];
cpu.gpr[29] = words[1];
cpu.gpr[30] = words[2];
for (std::size_t i = 0; i < 8; ++i) cpu.gpr[16 + i] = words[3 + i];
cpu.gpr[28] = words[11];
cpu.gpr[2] = 1u;
cpu.pc = words[0];
cpu.next_pc = words[0] + 4u;
cpu.pending_load = {};
cpu.delay_slot = {};
cpu.gpr[0] = 0u;
phase_ = Ps1InterruptContinuationPhase::hook_guest;
return {Ps1InterruptDriveStatus::guest_execution};
```

While `phase_ == hook_guest`, `drive()` returns `guest_execution` without changing state until guest code reaches an ordinary frontier or B0:17. Do not assign the callback return sentinel to hook code.

- [ ] **Step 5: Preserve strict malformed behavior**

Extend unit coverage so:

- unreadable IntRP node returns `terminal`;
- cyclic/repeated IntRP node returns `terminal`;
- unreadable custom HookEntryInt buffer returns `terminal` without partial register restore;
- an invalid callback address is not pre-skipped: runtime attempts the ordinary guest fetch and stops through the existing CPU boundary path;
- no device acknowledgement is synthesized by continuation completion.

Do not introduce a new public checkpoint format for these internal faults.

- [ ] **Step 6: Run all focused interrupt/HLE/CPU tests and verify GREEN**

```bash
cmake --build build --parallel 2 --target \
  jojo_ps1_interrupt_continuation_tests \
  jojo_ps1_cdrom_boot_runtime_tests \
  jojo_ps1_kernel_hle_frontier_tests \
  jojo_r3000a_cop0_tests
ctest --test-dir build -R 'jojo_(ps1_(interrupt_continuation|cdrom_boot_runtime|kernel_hle_frontier)|r3000a_cop0)_tests' --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 5**

```bash
git add src/core/ps1_interrupt_continuation.h src/core/ps1_interrupt_continuation.cpp tests/test_ps1_interrupt_continuation.cpp tests/test_ps1_cdrom_boot_runtime.cpp
git commit -m "feat: resume PS1 HookEntryInt guest continuation"
```

---

### Task 6: Lock MAX3 regression, strict non-expansion, and exact-SHA release evidence

**Files:**
- Modify/Test: `tests/test_ps1_max3_explorer.cpp`
- Modify/Test as needed only for regressions already specified: `tests/test_ps1_cdrom_state.cpp`, `tests/test_ps1_cdrom_strict_widths.cpp`
- No production behavior expansion is allowed in this task unless a RED exposes a defect in Tasks 1–5.

**Interfaces:**
- Consumes: complete interrupt continuation path and existing MAX3 `Ps1BootRuntime` copy/hash behavior.
- Produces: evidence that fake A0 disappears, continuation state is deterministic under copies, and the unsupported second CD-ROM command remains strict.

- [ ] **Step 1: Write RED MAX3 regression for the commercial-checkpoint shape**

Construct a fully synthetic executable that:

1. registers an IntRP priority-2 FIRST callback;
2. leaves `r9=0x35` before the interrupt;
3. raises the already-supported CD-ROM command-01 IRQ;
4. uses an all-zero exception vector;
5. has the FIRST callback return through B0:17;
6. then enters a harmless guest loop.

Run `explore_ps1_max3()` with a small deterministic budget and assert:

```cpp
CHECK(result);
CHECK(result.value.nodes.size() == 1u);
for (const auto& dependency : result.value.dependencies) {
    CHECK(!(dependency.kind == jojo::Ps1Max3DependencyKind::bios_frontier &&
            dependency.table == 0xA0u && dependency.selector == 0x35u));
}
CHECK(result.value.best_report.interrupts_accepted == 1u);
```

The exact node count may be greater than one only if the synthetic program intentionally exposes another real BIOS frontier; design the fixture so it does not.

- [ ] **Step 2: Add deterministic copy/hash coverage**

In `tests/test_ps1_interrupt_continuation.cpp`, copy an active continuation at each of these states and require equal hashes for equal copies and unequal hashes after changing only continuation-controlled future execution:

- dispatch priority/node state;
- FIRST active versus SECOND active;
- different saved resume PC or saved register;
- hook_guest versus dispatch.

This proves the value state itself is copy/hash deterministic. In `Ps1BootRuntime::diagnostic_state_hash()`, retain the explicit `hash_u64(...interrupt_continuation_.diagnostic_state_hash())` integration from Task 4; the MAX3 regression proves copied runtimes can traverse the path without false frontier deduplication.

- [ ] **Step 3: Reassert CD-ROM non-expansion**

Keep/add tests proving:

```cpp
// existing command 01 with an unread response remains unsupported on a second command 01
CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::unsupported);
```

Also preserve strict `0x1F801802` and 16/32-bit CD-ROM access behavior. Do not loosen these tests merely because the old commercial fallback path reached the second command.

- [ ] **Step 4: Run the full Linux verification suite**

Run from a clean Release build:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_observed_disc_revision.cpp build/libjojo_core.a -ldl -pthread -o observed_disc_revision_tests
./observed_disc_revision_tests
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_network_transport.cpp src/core/network_protocol.cpp -o network_transport_tests
./network_transport_tests
```

Expected: every command exits 0. Record the exact test count rather than assuming it remains 46 after adding the continuation test.

- [ ] **Step 5: Audit the final diff against the approved spec head**

Compare from `4ce652af118389d9c65dc45bc5c520359461d3a0` to the candidate final SHA. The changed-file set must be limited to the files named in this plan (plus the plan itself). Reject unrelated refactors or any proprietary payload.

Explicitly inspect that production diffs do **not** add:

- `A0:35` implementation;
- new CD-ROM commands/FIFO capacity;
- automatic I_STAT/HINTSTS clear;
- `B0:07 DeliverEvent` callback execution;
- DMA3/GPU drawing/presentation;
- BIOS ROM blobs or game-derived binary fixtures.

- [ ] **Step 6: Commit final test/audit changes**

```bash
git add tests/test_ps1_max3_explorer.cpp tests/test_ps1_interrupt_continuation.cpp tests/test_ps1_cdrom_state.cpp tests/test_ps1_cdrom_strict_widths.cpp
git commit -m "test: lock PS1 interrupt continuation frontier"
```

Only stage files actually changed.

- [ ] **Step 7: Verify GitHub Actions on the exact final SHA**

Wait for the repository `build` workflow for the exact candidate SHA. Require both jobs to finish `success`:

- `Portable core / Linux`
- `Windows x64 / MSVC 2022`

On Windows, require `Build Release`, production readiness, PS1 active architecture, `Test Release`, observed disc revision, direct UDP transport, and `Upload single executable` all to pass. Record the exact Windows test count.

Do not declare completion from a green older SHA.

- [ ] **Step 8: Verify the exact Windows artifact**

Download only the `JOJO-Recompiled-Windows-x64` artifact from the exact final workflow run. Verify:

```bash
sha256sum JOJO-Recompiled-Windows-x64-<short-final-sha>.zip
unzip -l JOJO-Recompiled-Windows-x64-<short-final-sha>.zip
```

The local ZIP SHA-256 must equal the digest reported by GitHub Actions, and the archive must contain exactly one production file: `JOJO-Recompiled.exe`. Extract it and record its SHA-256 as well.

- [ ] **Step 9: Request exactly one new commercial checkpoint**

Hand off the verified exact-SHA Windows ZIP and ask the user to run `EXECUTAR CHECKPOINT` exactly once. The next checkpoint is the authority for whether the next frontier is a real registered FIRST/SECOND callback, IRQ acknowledgement, HookEntryInt/B0:17 path, CPU/MMIO boundary, or another subsystem.

Do not claim gameplay, VRAM progress, or presented frames unless that checkpoint provides such evidence.
