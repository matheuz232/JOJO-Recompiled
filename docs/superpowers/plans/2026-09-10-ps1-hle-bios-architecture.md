# PS1 HLE BIOS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the growing PlayStation BIOS/kernel HLE logic from `Ps1BootRuntime` into a clean-room modular `Ps1HleBios` subsystem, preserve all already-validated commercial behavior, and add only the closed set of safe documented return-zero services approved by the design.

**Architecture:** `Ps1BootRuntime` remains responsible for CPU execution, PS1 memory bus, checkpoint reports, MAX3 frontiers, and platform policy. A new `Ps1HleBios` owns BIOS/kernel HLE state and dispatches A0/B0/C0/SYS requests through explicit request/result types. The generic R3000A executor continues to implement architectural `syscall` exceptions; PS1 SYS interception remains in the runtime and delegates only recognized SYS selectors to the HLE.

**Tech Stack:** C++20, CMake 3.20+, portable core, MSVC 2022 Windows x64, GCC/Linux CI, existing synthetic PS1 fixtures and MAX3 diagnostic tests.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-hle-bios-architecture-design.md`

## Global Constraints

- Do not embed, distribute, derive from, or require a proprietary Sony PlayStation BIOS ROM.
- Do not add proprietary game data or commercial checkpoint output to the repository.
- Keep `step_r3000a()` architectural `syscall` behavior unchanged.
- Keep MAX3 fallback diagnostic-only and preserve current limits: `max_nodes=5461`, `max_branch_depth=6`, `max_total_retired=1000000000`, per-segment `UINT64_MAX`, `trace_capacity=131072`, `mmio_event_capacity=65536`, `bios_event_capacity=65536`, `stagnation_instruction_limit=2000000`.
- Unknown A0/B0/C0 services must remain frontier-producing and must not mutate guest state.
- Unknown SYS services, including SYS03, must remain CPU boundaries in this tranche.
- Do not fabricate success for CD-ROM, GPU, event, thread, controller, memory-card, filesystem, or hardware-dependent services.
- A0/B0/C0 return through `$ra`; SYS completion advances the trapped instruction according to the existing runtime SYS semantics and must not be implemented as a BIOS-vector return.
- All code changes use TDD RED -> GREEN with fresh CI evidence before completion claims.

---

## File Structure

**Create**
- `src/core/ps1_hle_bios.h` — public request/result types, HLE state accessors, dispatch API, diagnostic state hash.
- `src/core/ps1_hle_bios.cpp` — focused A0/B0/C0/SYS dispatch and state mutations for this tranche.
- `tests/test_ps1_hle_bios.cpp` — direct HLE unit tests independent of the execution loop.

**Modify**
- `src/core/ps1_boot_runtime.h` — replace BIOS-owned state fields with one `Ps1HleBios`, retain compatibility forwarding accessors.
- `src/core/ps1_boot_runtime.cpp` — delegate A0/B0/C0/SYS behavior to `Ps1HleBios`, keep execution/frontier/report logic.
- `tests/test_ps1_boot_runtime.cpp` — runtime delegation/regression assertions.
- `tests/test_ps1_diagnostic_frontier.cpp` — SYS03 boundary regression and HLE-sensitive fingerprint regression.
- `tests/test_ps1_max3_explorer.cpp` — MAX3 dedup remains sensitive to HLE state.
- `CMakeLists.txt` — compile `ps1_hle_bios.cpp` and register `jojo_ps1_hle_bios_tests`.

---

### Task 1: Introduce `Ps1HleBios` types and state container

**Files:**
- Create: `src/core/ps1_hle_bios.h`
- Create: `src/core/ps1_hle_bios.cpp`
- Create: `tests/test_ps1_hle_bios.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class Ps1HleBiosDomain : std::uint8_t { a0, b0, c0, sys };`
  - `struct Ps1HleBiosCall { Ps1HleBiosDomain domain; std::uint32_t selector, pc, a0, a1, a2, a3, ra; };`
  - `enum class Ps1HleBiosDisposition : std::uint8_t { handled, unsupported, terminal };`
  - `struct Ps1HleBiosResult { Ps1HleBiosDisposition disposition; };`
  - `class Ps1HleBios` with `dispatch(...)`, state accessors, and `diagnostic_state_hash()`.

- [ ] **Step 1: Write the failing state-container test**

Create `tests/test_ps1_hle_bios.cpp` with a minimal synthetic CPU and assert that two default `Ps1HleBios` objects have the same hash, then mutate one through a supported observed call and assert their hashes differ:

```cpp
#include "core/ps1_hle_bios.h"
#include "core/r3000a_state.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void test_default_hash_is_deterministic_and_state_changes_hash() {
    jojo::Ps1HleBios first;
    jojo::Ps1HleBios second;
    CHECK(first.diagnostic_state_hash() == second.diagnostic_state_hash());

    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010040u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::a0,
        0x39u,
        0x000000A0u,
        0x00004000u,
        0x00001000u,
        0u,
        0u,
        cpu.gpr[31],
    };
    CHECK(first.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(first.diagnostic_state_hash() != second.diagnostic_state_hash());
}

int main() {
    test_default_hash_is_deterministic_and_state_changes_hash();
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Register the test and verify RED**

Add to `CMakeLists.txt`:

```cmake
add_jojo_test(jojo_ps1_hle_bios_tests tests/test_ps1_hle_bios.cpp)
```

Run CI on the branch. Expected RED: compilation fails because `core/ps1_hle_bios.h` and the new types do not exist yet.

- [ ] **Step 3: Implement the minimal public component**

Create `src/core/ps1_hle_bios.h` with the normative spec types plus:

```cpp
struct Ps1BiosHeapState {
    std::uint32_t base{};
    std::uint32_t size{};
};

class Ps1HleBios {
public:
    [[nodiscard]] Ps1HleBiosResult dispatch(
        const Ps1HleBiosCall& call,
        R3000aState& cpu) noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(std::uint32_t counter) const noexcept;
    [[nodiscard]] bool iso9660_removed() const noexcept;

private:
    std::optional<Ps1BiosHeapState> heap_state_{};
    std::optional<std::uint32_t> interrupt_hook_address_{};
    std::optional<bool> pad_card_auto_ack_enabled_{};
    std::array<std::optional<bool>, 4> root_counter_auto_ack_enabled_{};
    bool iso9660_removed_{};
};
```

Create `src/core/ps1_hle_bios.cpp` with only enough A0/39 `InitHeap` support to make the test meaningful. A0/B0/C0 vector returns use:

```cpp
static void return_from_bios_vector(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}
```

Add `src/core/ps1_hle_bios.cpp` to `jojo_core` in `CMakeLists.txt`.

- [ ] **Step 4: Verify GREEN**

Run CI. Expected: new HLE test passes and all existing tests remain green.

- [ ] **Step 5: Commit**

Commit message:

```text
feat: introduce modular PS1 HLE BIOS state
```

---

### Task 2: Migrate observed A0 services into `Ps1HleBios`

**Files:**
- Modify: `src/core/ps1_hle_bios.cpp`
- Modify: `tests/test_ps1_hle_bios.cpp`
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

**Interfaces:**
- Consumes: `Ps1HleBios::dispatch`, HLE state accessors from Task 1.
- Produces: A0/39 `InitHeap`, A0/56 and A0/72 `_96_remove` owned by `Ps1HleBios`.

- [ ] **Step 1: Add RED unit tests for observed A0 behavior**

Extend `tests/test_ps1_hle_bios.cpp` to assert:

```cpp
static void test_a0_initheap_records_state_and_returns_via_ra() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000A0u;
    cpu.next_pc = 0x000000A4u;
    cpu.gpr[31] = 0x80012340u;

    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::a0, 0x39u, cpu.pc,
        0x00004000u, 0x00001000u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.pc == 0x80012340u);
    CHECK(bios.heap_state().has_value());
    CHECK(bios.heap_state()->base == 0x00004000u);
    CHECK(bios.heap_state()->size == 0x00001000u);
}

static void test_a0_remove_aliases_preserve_v0_and_mark_logical_state() {
    for (const std::uint32_t selector : {0x56u, 0x72u}) {
        jojo::Ps1HleBios bios;
        jojo::R3000aState cpu{};
        cpu.gpr[2] = 0x12345678u;
        cpu.gpr[31] = 0x80010080u;
        const jojo::Ps1HleBiosCall call{
            jojo::Ps1HleBiosDomain::a0, selector, 0xA0u,
            0u, 0u, 0u, 0u, cpu.gpr[31]};
        CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
        CHECK(cpu.gpr[2] == 0x12345678u);
        CHECK(bios.iso9660_removed());
    }
}
```

Temporarily change the HLE implementation so only A0/39 exists. Expected RED: `_96_remove` tests return `unsupported`.

- [ ] **Step 2: Implement A0/56 and A0/72**

In `dispatch_a0`, implement exactly:

```cpp
case 0x39u:
    heap_state_ = Ps1BiosHeapState{call.a0, call.a1};
    return_from_bios_vector(cpu);
    return {Ps1HleBiosDisposition::handled};
case 0x56u:
case 0x72u:
    iso9660_removed_ = true;
    return_from_bios_vector(cpu);
    return {Ps1HleBiosDisposition::handled};
default:
    return {Ps1HleBiosDisposition::unsupported};
```

- [ ] **Step 3: Move runtime A0 ownership**

Add `Ps1HleBios hle_bios_{};` to `Ps1BootRuntime`, remove `bios_heap_state_` and `bios_iso9660_removed_`, and make existing public runtime accessors forward to `hle_bios_`:

```cpp
const std::optional<Ps1BiosHeapState>& Ps1BootRuntime::bios_heap_state() const noexcept {
    return hle_bios_.heap_state();
}

bool Ps1BootRuntime::bios_iso9660_removed() const noexcept {
    return hle_bios_.iso9660_removed();
}
```

When PC resolves to A0/B0/C0, construct `Ps1HleBiosCall` from current CPU registers and call `hle_bios_.dispatch(...)` instead of the old inline `handle_bios_call` for migrated selectors.

- [ ] **Step 4: Verify runtime regressions**

Run `jojo_ps1_hle_bios_tests`, `jojo_ps1_boot_runtime_tests`, `jojo_ps1_96_remove_tests`, then full CI. Expected GREEN.

- [ ] **Step 5: Commit**

```text
refactor: migrate observed A0 BIOS services to HLE
```

---

### Task 3: Migrate observed B0/C0 services and BIOS-owned state

**Files:**
- Modify: `src/core/ps1_hle_bios.cpp`
- Modify: `tests/test_ps1_hle_bios.cpp`
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`
- Existing tests: `tests/test_ps1_changeclearrcnt.cpp`

**Interfaces:**
- Produces: B0/19 `HookEntryInt`, B0/5B `ChangeClearPAD`, C0/0A `ChangeClearRCnt` fully owned by HLE.

- [ ] **Step 1: Write RED tests for stateful B0/C0 handlers**

Add direct HLE tests:

```cpp
static void test_b0_hookentryint_records_pointer() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010100u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::b0, 0x19u, 0xB0u,
        0x800616F0u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(bios.interrupt_hook_address().has_value());
    CHECK(*bios.interrupt_hook_address() == 0x800616F0u);
}

static void test_b0_changeclearpad_records_flag() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010100u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::b0, 0x5Bu, 0xB0u,
        0u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(bios.pad_card_auto_ack_enabled().has_value());
    CHECK(!*bios.pad_card_auto_ack_enabled());
}

static void test_c0_changeclearrcnt_returns_previous_flag() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80010100u;
    jojo::Ps1HleBiosCall clear{
        jojo::Ps1HleBiosDomain::c0, 0x0Au, 0xC0u,
        3u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(clear, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    cpu.gpr[31] = 0x80010120u;
    auto set = clear;
    set.a1 = 1u;
    set.ra = cpu.gpr[31];
    CHECK(bios.dispatch(set, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 0u);
    CHECK(bios.root_counter_auto_ack_enabled(3u) == std::optional<bool>{true});
}
```

Expected RED: selectors return `unsupported` until implemented.

- [ ] **Step 2: Implement B0/C0 dispatch**

Implement only the approved observed selectors. For C0/0A, reject `call.a0 >= 4` with `unsupported` and no mutation.

- [ ] **Step 3: Remove remaining BIOS state from runtime**

Delete runtime-owned fields:

```cpp
std::optional<std::uint32_t> bios_interrupt_hook_address_{};
std::optional<bool> bios_pad_card_auto_ack_enabled_{};
std::array<std::optional<bool>, 4> bios_root_counter_auto_ack_enabled_{};
```

Forward existing public accessors to `hle_bios_`.

- [ ] **Step 4: Verify GREEN**

Run direct HLE, boot runtime, `ChangeClearRCnt`, and full CI. Expected all green.

- [ ] **Step 5: Commit**

```text
refactor: migrate B0 and C0 BIOS services to HLE
```

---

### Task 4: Move SYS00/SYS01/SYS02 policy into `Ps1HleBios`

**Files:**
- Modify: `src/core/ps1_hle_bios.cpp`
- Modify: `src/core/ps1_hle_bios.h`
- Modify: `tests/test_ps1_hle_bios.cpp`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_diagnostic_frontier.cpp`

**Interfaces:**
- Produces: HLE-side SYS state transition logic while runtime keeps the interception eligibility policy.

- [ ] **Step 1: Add RED direct SYS tests**

Use `domain=sys`, selector in `call.selector`, and require:

```cpp
static void test_sys00_preserves_registers_and_advances_instruction() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x80010000u;
    cpu.next_pc = 0x80010004u;
    cpu.gpr[2] = 0x12345678u;
    const jojo::Ps1HleBiosCall call{
        jojo::Ps1HleBiosDomain::sys, 0u, cpu.pc,
        0u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(call, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.pc == 0x80010004u);
    CHECK(cpu.gpr[2] == 0x12345678u);
}

static void test_sys01_sys02_match_critical_section_contract() {
    jojo::Ps1HleBios bios;
    jojo::R3000aState cpu{};
    cpu.pc = 0x80010000u;
    cpu.next_pc = 0x80010004u;
    cpu.cop0.status = 0x00000401u;

    jojo::Ps1HleBiosCall enter{
        jojo::Ps1HleBiosDomain::sys, 1u, cpu.pc,
        1u, 0u, 0u, 0u, cpu.gpr[31]};
    CHECK(bios.dispatch(enter, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == 1u);
    CHECK((cpu.cop0.status & 0x00000401u) == 0u);

    cpu.next_pc = cpu.pc + 4u;
    jojo::Ps1HleBiosCall exit{
        jojo::Ps1HleBiosDomain::sys, 2u, cpu.pc,
        2u, 0u, 0u, 0u, cpu.gpr[31]};
    const auto preserved_v0 = cpu.gpr[2];
    CHECK(bios.dispatch(exit, cpu).disposition == jojo::Ps1HleBiosDisposition::handled);
    CHECK(cpu.gpr[2] == preserved_v0);
    CHECK((cpu.cop0.status & 0x00000401u) == 0x00000401u);
}
```

Also assert SYS03 returns `unsupported` and leaves CPU unchanged.

- [ ] **Step 2: Implement HLE SYS dispatcher**

Move the current already-green state mutation logic into `dispatch_sys`. `Ps1HleBios::dispatch` may directly advance CPU for SYS, but it must not decide whether an architectural interrupt/delay-slot condition preempts the syscall; that remains runtime policy.

Use the same critical mask already validated:

```cpp
constexpr std::uint32_t kCriticalMask = (1u << 0) | (1u << 10);
```

- [ ] **Step 3: Reduce runtime SYS helper to eligibility + delegation**

Keep in `Ps1BootRuntime`:

```cpp
if (report.last_opcode && is_syscall_opcode(*report.last_opcode) &&
    !cpu_.delay_slot.active && !interrupt_would_preempt_syscall(cpu_)) {
    Ps1HleBiosCall call{Ps1HleBiosDomain::sys, cpu_.gpr[4], cpu_.pc,
                        cpu_.gpr[4], cpu_.gpr[5], cpu_.gpr[6], cpu_.gpr[7], cpu_.gpr[31]};
    if (hle_bios_.dispatch(call, cpu_).disposition == Ps1HleBiosDisposition::handled) {
        retire_pending_load_for_hle(cpu_);
        ++report.instructions_retired;
        ...
        continue;
    }
}
```

Preserve the tested load-delay ordering exactly: retire the pending load before applying the SYS semantic mutation, matching the current green implementation.

- [ ] **Step 4: Verify SYS03 and generic CPU syscall regressions**

Run:

```text
jojo_ps1_hle_bios_tests
jojo_ps1_diagnostic_frontier_tests
jojo_r3000a_exception_tests
jojo_r3000a_boundary_tests
```

Expected GREEN; generic `step_r3000a()` still raises architectural syscall exception.

- [ ] **Step 5: Commit**

```text
refactor: delegate PS1 SYS services to HLE BIOS
```

---

### Task 5: Add the closed safe documented return-zero selector set

**Files:**
- Modify: `src/core/ps1_hle_bios.cpp`
- Modify: `tests/test_ps1_hle_bios.cpp`

**Interfaces:**
- Produces only the exact closed selectors approved by the spec; no ranges are interpreted dynamically beyond those values.

- [ ] **Step 1: Write one table-driven RED test covering every approved selector**

Add explicit constexpr arrays:

```cpp
static constexpr std::uint32_t kSafeA0ReturnZero[] = {
    0x57u, 0x58u, 0x59u, 0x5Au,
    0x73u, 0x74u, 0x75u, 0x76u, 0x77u,
    0x79u, 0x7Au, 0x7Bu, 0x7Du,
    0x7Fu, 0x80u,
    0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u, 0x89u,
    0x8Au, 0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu,
    0xB0u, 0xB1u, 0xB3u,
};

static constexpr std::uint32_t kSafeC0ReturnZero[] = {
    0x0Eu, 0x0Fu, 0x10u, 0x11u, 0x14u,
};
```

For each selector set `$v0 = 0xDEADBEEF`, dispatch it, and assert `handled`, `$v0 == 0`, and return via `$ra`.

Also test nearby excluded selectors such as A0/5B, A0/78, A0/7C, A0/7E, A0/81, A0/B2, C0/12, C0/13, C0/15 as `unsupported` with CPU unchanged.

Expected RED: all newly approved selectors are unsupported.

- [ ] **Step 2: Implement exact selector predicates**

Use explicit helpers, not broad numeric ranges:

```cpp
static bool is_safe_a0_return_zero(std::uint32_t selector) noexcept {
    switch (selector) {
        case 0x57u: case 0x58u: case 0x59u: case 0x5Au:
        case 0x73u: case 0x74u: case 0x75u: case 0x76u: case 0x77u:
        case 0x79u: case 0x7Au: case 0x7Bu: case 0x7Du:
        case 0x7Fu: case 0x80u:
        case 0x82u: case 0x83u: case 0x84u: case 0x85u:
        case 0x86u: case 0x87u: case 0x88u: case 0x89u:
        case 0x8Au: case 0x8Bu: case 0x8Cu: case 0x8Du:
        case 0x8Eu: case 0x8Fu:
        case 0xB0u: case 0xB1u: case 0xB3u:
            return true;
        default:
            return false;
    }
}
```

Do the same for C0. Handler body:

```cpp
cpu.gpr[2] = 0u;
return_from_bios_vector(cpu);
return {Ps1HleBiosDisposition::handled};
```

- [ ] **Step 3: Verify excluded selectors remain unsupported**

Run only `jojo_ps1_hle_bios_tests` first; expected GREEN. Then full CI.

- [ ] **Step 4: Commit**

```text
feat: add safe documented PS1 BIOS HLE stubs
```

---

### Task 6: Integrate HLE state into MAX3 fingerprint and preserve frontiers

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_diagnostic_frontier.cpp`
- Modify: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: `Ps1HleBios::diagnostic_state_hash()`.
- Produces: runtime fingerprint that includes HLE state; unchanged A0/B0/C0 frontier semantics for unsupported selectors.

- [ ] **Step 1: Write RED fingerprint regression**

Create two identical runtimes. In one, execute A0/39 through normal runtime HLE; in the other, do not. Assert CPU/bus can otherwise converge but `diagnostic_state_hash()` differs because the HLE state differs.

Example structure:

```cpp
auto baseline = make_runtime(...);
auto mutated = baseline;
// Drive only mutated through an HLE service that changes HLE-owned state.
CHECK(mutated.diagnostic_state_hash() != baseline.diagnostic_state_hash());
```

Expected RED after temporarily removing old runtime-owned state hashing: hashes collide until HLE hash is explicitly incorporated.

- [ ] **Step 2: Combine HLE hash into runtime hash**

In `Ps1BootRuntime::diagnostic_state_hash()` hash:

```cpp
hash_u64(hash, hle_bios_.diagnostic_state_hash());
```

Remove the old individual BIOS fields from runtime hashing because they no longer exist.

- [ ] **Step 3: Assert unsupported BIOS still produces exact MAX3 frontier**

Keep A0/33 as the regression service. Test must still show:

```cpp
report.stop_reason == Ps1BootStopReason::bios_call_unimplemented
report.recent_bios_calls.back().table_physical == 0xA0u
report.recent_bios_calls.back().selector == 0x33u
```

and `apply_diagnostic_bios_fallback(...)` remains branchable from that state.

- [ ] **Step 4: Verify MAX3 dedup sensitivity**

Extend `test_ps1_max3_explorer.cpp` so two branches with equal PC/registers but different HLE state hashes are not considered convergent.

- [ ] **Step 5: Run full CI and commit**

Expected GREEN on Linux and Windows.

Commit:

```text
refactor: make MAX3 fingerprints HLE BIOS aware
```

---

### Task 7: Remove obsolete inline BIOS dispatcher and finish migration cleanup

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_hle_bios.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

**Interfaces:**
- Produces: one source of truth for BIOS guest-visible state and service semantics.

- [ ] **Step 1: Add a static/runtime regression that all migrated selectors are handled through the new component**

Use existing runtime tests plus direct HLE tests. No duplicate service behavior should remain in `ps1_boot_runtime.cpp`.

- [ ] **Step 2: Delete old inline helper ownership**

Remove from `ps1_boot_runtime.cpp` the old `handle_bios_call` implementation and BIOS-specific state hash helpers that are now internal to `Ps1HleBios`. Retain only orchestration helpers such as report recording, SYS eligibility, diagnostic fallback, and generic runtime hashing.

- [ ] **Step 3: Verify source-of-truth cleanup**

Search the branch for old field names:

```text
bios_heap_state_
bios_interrupt_hook_address_
bios_pad_card_auto_ack_enabled_
bios_root_counter_auto_ack_enabled_
bios_iso9660_removed_
```

Expected: none remain as `Ps1BootRuntime` members; equivalent state exists only in `Ps1HleBios`.

- [ ] **Step 4: Run all tests**

Expected: all existing PS1 CPU/runtime/MAX3 tests plus new HLE tests pass.

- [ ] **Step 5: Commit**

```text
refactor: complete PS1 HLE BIOS migration
```

---

### Task 8: Final commercial-ready CI gate and Windows artifact

**Files:**
- No production code unless a verified CI failure requires a bounded fix.
- Review all files changed from baseline `a700b534e5181fd0a6b19ec801f850397b2ba903` through final HEAD.

**Interfaces:**
- Produces: exact final SHA, CI run, and Windows x64 artifact suitable for the next MAX3 commercial checkpoint.

- [ ] **Step 1: Compare final branch against the SYS baseline**

Verify the diff is restricted to:

```text
src/core/ps1_hle_bios.h
src/core/ps1_hle_bios.cpp
src/core/ps1_boot_runtime.h
src/core/ps1_boot_runtime.cpp
CMakeLists.txt
tests/test_ps1_hle_bios.cpp
tests/test_ps1_boot_runtime.cpp
tests/test_ps1_diagnostic_frontier.cpp
tests/test_ps1_max3_explorer.cpp
docs/superpowers/specs/2026-09-10-ps1-hle-bios-architecture-design.md
docs/superpowers/plans/2026-09-10-ps1-hle-bios-architecture.md
```

If a required test file differs, include it only when directly necessary for the migration and explain it in the final diff review.

- [ ] **Step 2: Verify final Linux job**

Require all of these on the exact final SHA:

```text
Configure: success
Build: success
Production readiness gate: success
PS1 active architecture gate: success
Test: success
Observed disc revision contract: success
R2.5 direct UDP transport contract: success
```

- [ ] **Step 3: Verify final Windows x64/MSVC job**

Require:

```text
Configure: success
Build Release: success
Production readiness gate: success
PS1 active architecture gate: success
Test Release: success
Observed disc revision contract: success
R2.5 direct UDP transport contract: success
Upload single executable: success
```

- [ ] **Step 4: Download and verify artifact**

Fetch workflow artifact `JOJO-Recompiled-Windows-x64`, download the ZIP, verify GitHub digest and local SHA-256 match, and verify it contains `JOJO-Recompiled.exe`.

- [ ] **Step 5: Commercial checkpoint acceptance**

User runs the exact artifact and produces `%LOCALAPPDATA%\JOJO Recompiled\diagnostics\m3a-checkpoint.txt`. Acceptance evidence must start with:

```text
format=jojo-max3-checkpoint-v1
```

It must not regress to any migrated A0/B0/C0/SYS selector as an unsupported frontier. The next stop may be a genuinely unsupported BIOS family, SYS03+, MMIO/hardware dependency, or another CPU boundary.

---

## Self-Review Checklist

- Spec coverage: all normative architecture, state ownership, A0/B0/C0/SYS migration, closed safe selector list, MAX3 preservation, unsupported-service behavior, CI gates, and commercial acceptance are assigned to concrete tasks.
- Placeholder scan: no TBD/TODO/open-ended implementation step remains.
- Type consistency: `Ps1HleBiosCall`, `Ps1HleBiosDomain`, `Ps1HleBiosDisposition`, `Ps1HleBiosResult`, `Ps1HleBios::dispatch`, and `Ps1HleBios::diagnostic_state_hash` match the approved spec.
- Return-path consistency: BIOS vectors and SYS use distinct completion mechanisms.
- Safety/compatibility: no hardware-dependent service is included in the return-zero batch; excluded selector regressions are explicitly tested.
