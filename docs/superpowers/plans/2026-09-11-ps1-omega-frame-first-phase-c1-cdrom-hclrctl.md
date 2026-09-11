# PS1 OMEGA Frame-First Phase C1 CD-ROM HCLRCTL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cross JoJo's strict `write8 0x1F801803 = 0x07` CD-ROM blocker by implementing only bank-1 HCLRCTL acknowledge bits 0–4, preserving fail-closed behavior for bits 5–7 and preserving separate ownership of CD-ROM IRQ state versus bus `I_STAT`.

**Architecture:** Extend only `Ps1CdromState::write8()` for the bank-1 HCLRCTL register. The memory bus already routes byte writes at `0x1F801803` into `Ps1CdromState`, so no new MMIO mapping or public API is required. Prove the behavior first at device level, then at bus level, then run full Linux/Windows CI and require a later strict commercial checkpoint.

**Tech Stack:** C++20 core runtime, existing `Ps1CdromState`, `Ps1MemoryBus`, CMake/CTest, GitHub Actions Linux + Windows x64/MSVC, MAX3/OMEGA commercial checkpoint.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-phase-c1-cdrom-hclrctl-design.md`

## Global Constraints

- Production baseline SHA: `be2645471daff4f43d6cae91f15b0b755b04b419`.
- Authoritative blocker: strict byte write `0x07` to `0x1F801803` at PC `0x8004B494`, opcode `0xA0430000`, after 816,561 retired instructions.
- Supported C1 HCLRCTL semantics: bank 1, bits 0–4 only.
- Any bank-1 HCLRCTL value with `value & 0xE0 != 0` remains unsupported and must not mutate device state.
- Supported HCLRCTL acknowledge clears the corresponding bits in `interrupt_status_`, drains the currently modeled single response FIFO, recomputes `irq_line_`, and returns `ok`.
- HCLRCTL must not clear `Ps1MemoryBus::interrupt_status_`; the already latched CD-ROM `I_STAT` bit remains until guest software acknowledges `I_STAT` through existing interrupt-controller semantics.
- No new CD-ROM commands, timing, data/sector FIFO, DMA, GPU, GTE, BIOS, SIO, SPU, or diagnostic fallback semantics.
- Synthetic GREEN is insufficient for commercial completion; a new strict checkpoint must pass the observed HCLRCTL frontier.

---

### Task 1: Device-level RED for observed HCLRCTL acknowledge

**Files:**
- Modify: `tests/test_ps1_cdrom_state.cpp`
- Production files unchanged

**Interfaces:**
- Consumes: existing `Ps1CdromState::seed_post_bios`, `write8`, `read8`, `interrupt_status`, `irq_line`, `diagnostic_state_hash`
- Produces: a failing contract that exactly captures the supported bank-1 `0x07` acknowledge and fail-closed high-bit behavior

- [ ] **Step 1: Add a helper that creates the observed pending-IRQ state**

Add a focused helper next to the existing test helpers:

```cpp
static void prepare_getstat_irq(jojo::Ps1CdromState& cdrom) {
    cdrom.seed_post_bios(0x02u, 0x1Fu);
    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.interrupt_status() == 3u);
    CHECK(cdrom.irq_line());
}
```

This uses only already-supported Getstat behavior.

- [ ] **Step 2: Add the observed-path RED test**

Add:

```cpp
static void test_bank1_hclrctl_acknowledges_getstat_irq() {
    jojo::Ps1CdromState cdrom;
    prepare_getstat_irq(cdrom);

    const auto response = cdrom.read8(0x1F801801u);
    CHECK(response.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(response.value == 0x02u);

    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    const auto before_ack = cdrom.diagnostic_state_hash();
    const auto ack = cdrom.write8(0x1F801803u, 0x07u);

    CHECK(ack.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.interrupt_status() == 0u);
    CHECK(!cdrom.irq_line());
    CHECK(cdrom.diagnostic_state_hash() != before_ack);
}
```

Call it from `main()`.

- [ ] **Step 3: Add fail-closed RED/GREEN cases for bits 5–7**

Add:

```cpp
static void test_bank1_hclrctl_rejects_unmodeled_side_effect_bits() {
    for (const std::uint8_t value : {0x20u, 0x40u, 0x80u, 0x27u, 0x47u, 0x87u}) {
        jojo::Ps1CdromState cdrom;
        prepare_getstat_irq(cdrom);
        CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);

        const auto before = cdrom.diagnostic_state_hash();
        const auto result = cdrom.write8(0x1F801803u, value);

        CHECK(result.status == jojo::Ps1CdromIoStatus::unsupported_register);
        CHECK(cdrom.diagnostic_state_hash() == before);
        CHECK(cdrom.interrupt_status() == 3u);
        CHECK(cdrom.irq_line());
    }
}
```

Also preserve the existing bank-0 non-zero rejection tests.

- [ ] **Step 4: Run the device test and verify RED**

Run after the normal configure/build step:

```bash
ctest --test-dir build -R '^jojo_ps1_cdrom_state_tests$' --output-on-failure
```

Expected: FAIL specifically because bank-1 `write8(0x1F801803, 0x07)` returns `unsupported_register`. The new high-bit rejection cases may already pass; that is acceptable because the observed-path assertion is the required RED.

- [ ] **Step 5: Commit the RED tests only**

```bash
git add tests/test_ps1_cdrom_state.cpp
git commit -m "test: require CD-ROM HCLRCTL acknowledge"
```

Do not modify production code in this commit.

---

### Task 2: Minimal GREEN in `Ps1CdromState`

**Files:**
- Modify: `src/core/ps1_cdrom_state.cpp`
- Test: `tests/test_ps1_cdrom_state.cpp`
- Do not modify: `src/core/ps1_cdrom_state.h`

**Interfaces:**
- Consumes: private `index_`, `interrupt_status_`, `response_`, `recompute_irq()`
- Produces: supported bank-1 HCLRCTL acknowledge for values containing only bits 0–4

- [ ] **Step 1: Implement the bank-1 HCLRCTL branch before the existing bank-0 request case**

Change the `physical == kCdromRequestInterrupt` block to the following shape:

```cpp
if (physical == kCdromRequestInterrupt) {
    if (index_ == 1u) {
        if ((value & 0xE0u) != 0u) {
            return {};
        }
        interrupt_status_ = static_cast<std::uint8_t>(
            interrupt_status_ & static_cast<std::uint8_t>(~value) & 0x1Fu);
        response_.reset();
        recompute_irq();
        return {Ps1CdromIoStatus::ok, 0u};
    }
    if (index_ == 0u && value == 0u) {
        return {Ps1CdromIoStatus::ok, 0u};
    }
    return {};
}
```

Do not add new fields or public methods.

- [ ] **Step 2: Run the focused device test and verify GREEN**

```bash
ctest --test-dir build -R '^jojo_ps1_cdrom_state_tests$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 3: Review state-transition invariants**

Confirm from the test output/code review:

```text
bank1 + 0x07 -> ok
interrupt_status: 3 -> 0
irq_line: true -> false
bits 5-7 -> unsupported_register
unsupported high-bit write -> diagnostic hash unchanged
bank0 + 0x00 -> unchanged existing behavior
```

- [ ] **Step 4: Commit the minimal device implementation**

```bash
git add src/core/ps1_cdrom_state.cpp
git commit -m "feat: acknowledge PS1 CD-ROM HCLRCTL IRQ flags"
```

---

### Task 3: Bus-level RED/GREEN contract and interrupt-controller separation

**Files:**
- Modify: `tests/test_ps1_memory_bus.cpp`
- Production files expected unchanged

**Interfaces:**
- Consumes: `Ps1MemoryBus::write8`, `read8`, `read16`, `write16`, `interrupt_status`, `cdrom`, `last_diagnostic_mmio_probe`, `clear_last_diagnostic_mmio_probe`, `last_unsupported_access`, `clear_last_unsupported_access`
- Produces: proof that the exact commercial write crosses through production CD-ROM semantics and leaves bus `I_STAT` latched

- [ ] **Step 1: Add a focused bus test block after the existing CD-ROM bus tests**

Add:

```cpp
{
    jojo::Ps1MemoryBus bus;
    bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    bus.set_diagnostic_mmio_probe_enabled(true);

    CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_status() == 0x0004u);
    CHECK(bus.cdrom().interrupt_status() == 3u);
    CHECK(bus.cdrom().irq_line());

    const auto response = bus.read8(0x1F801801u);
    CHECK(response.status == jojo::R3000aBusStatus::ok);
    CHECK(response.value == 0x02u);

    CHECK(bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    bus.clear_last_diagnostic_mmio_probe();
    bus.clear_last_unsupported_access();

    const auto ack = bus.write8(0x1F801803u, 0x07u);
    CHECK(ack.status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(!bus.last_unsupported_access().has_value());
    CHECK(bus.cdrom().interrupt_status() == 0u);
    CHECK(!bus.cdrom().irq_line());

    CHECK(bus.interrupt_status() == 0x0004u);
    CHECK(bus.read16(0x1F801070u).value == 0x0004u);
    CHECK(bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_status() == 0u);
}
```

- [ ] **Step 2: Validate bus-level behavior**

Run:

```bash
ctest --test-dir build -R '^jojo_ps1_memory_bus_tests$' --output-on-failure
```

Expected after Task 2: PASS without any `Ps1MemoryBus` production change. If it fails because the bus reroutes or mutates `I_STAT`, fix only the smallest verified integration defect; do not expand device scope.

- [ ] **Step 3: Run both focused suites together**

```bash
ctest --test-dir build -R '^jojo_ps1_(cdrom_state|memory_bus)_tests$' --output-on-failure
```

Expected: 100% PASS.

- [ ] **Step 4: Commit the bus integration contract**

```bash
git add tests/test_ps1_memory_bus.cpp
git commit -m "test: preserve I_STAT across CD-ROM HCLRCTL ack"
```

---

### Task 4: Regression, determinism, and scope audit

**Files:**
- Read/verify: all changed files since baseline
- No unrelated production modifications

**Interfaces:**
- Consumes: completed Task 1–3 commits
- Produces: proof that C1 did not widen CD-ROM or hardware semantics beyond the approved blocker

- [ ] **Step 1: Run the complete CTest suite after configure/build**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests PASS. GitHub Actions remains authoritative for Linux and Windows platform validation in Task 5.

- [ ] **Step 2: Audit changed files against the baseline**

The production diff should contain only:

```text
src/core/ps1_cdrom_state.cpp
```

Test/docs changes may include:

```text
tests/test_ps1_cdrom_state.cpp
tests/test_ps1_memory_bus.cpp
docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-phase-c1-cdrom-hclrctl-design.md
docs/superpowers/plans/2026-09-11-ps1-omega-frame-first-phase-c1-cdrom-hclrctl.md
```

Any GPU, DMA, GTE, BIOS, timer, SIO, SPU, runtime, or search-policy production diff requires explicit rejection/re-scope.

- [ ] **Step 3: Re-check deterministic/fail-closed invariants**

Confirm:

```text
identical seed + identical HCLRCTL sequence -> identical diagnostic_state_hash
unsupported high-bit HCLRCTL write -> no state mutation
unsupported bank/register combinations remain unsupported
no diagnostic MMIO override is consumed for the supported side-effectful write
```

- [ ] **Step 4: Commit only if an actual test-only cleanup was necessary**

If no cleanup is required, do not create an empty commit.

---

### Task 5: Linux + Windows authoritative CI and artifact gate

**Files:**
- CI only
- No code changes while the final SHA is under validation

**Interfaces:**
- Consumes: final C1 implementation SHA
- Produces: one Linux/Windows-green SHA and one Windows x64 artifact from that exact SHA

- [ ] **Step 1: Finalize the implementation branch and record the exact head SHA**

The SHA becomes immutable for this validation pass.

- [ ] **Step 2: Require Linux CI success**

Require the standard workflow to pass configure/build, readiness/architecture gates, CTest, and contract checks on the exact final SHA.

- [ ] **Step 3: Require Windows x64/MSVC success**

Require the same final SHA to pass Release build, readiness/architecture gates, CTest, contracts, and executable packaging.

- [ ] **Step 4: Record the Windows artifact provenance**

Record the artifact name, artifact ID, artifact size, artifact digest, workflow run ID, and exact final SHA. Do not use an artifact from an earlier commit.

---

### Task 6: Commercial C1 exit checkpoint

**Files:**
- User-local output only: `%LOCALAPPDATA%\JOJO Recompiled\diagnostics\m3a-checkpoint.txt`
- No proprietary payloads committed

**Interfaces:**
- Consumes: Windows artifact from Task 5 and user's legal local `ps1_m1` installation
- Produces: authoritative evidence that the 816,561-instruction HCLRCTL blocker was crossed, or evidence that C1 is not commercially complete

- [ ] **Step 1: Run `EXECUTAR CHECKPOINT` with the exact final C1 artifact**

Use the same local installation used for C0.

- [ ] **Step 2: Inspect the new strict report**

Require both:

```text
terminal event is strict/non-speculative
terminal blocker is not the same bank-1 0x1F801803 = 0x07 write
```

And at least one of:

```text
instructions_retired > 816561
terminal strict frontier is later than the HCLRCTL write
new strict Frame-First landmark is reached
```

- [ ] **Step 3: Close or reject C1**

If the checkpoint passes the gate, C1 is commercially complete and the new terminal strict blocker becomes the next evidence input.

If the checkpoint stops again at the same HCLRCTL write, do not declare completion; inspect the bounded diagnostics and return to the smallest failing contract.

---

## Expected Commit Sequence

```text
test: require CD-ROM HCLRCTL acknowledge
feat: acknowledge PS1 CD-ROM HCLRCTL IRQ flags
test: preserve I_STAT across CD-ROM HCLRCTL ack
```

Additional commits require a concrete discovered defect; do not add speculative cleanup or unrelated refactors.

## C1 Completion Record

C1 is complete only when the record contains the exact baseline SHA and blocker identity above, plus the recorded final implementation SHA from Task 5, green synthetic device and bus contracts, green Linux/Windows CI, artifact provenance, and a strict commercial checkpoint that reaches a later frontier or new Frame-First landmark.

Synthetic tests alone do not satisfy commercial completion.