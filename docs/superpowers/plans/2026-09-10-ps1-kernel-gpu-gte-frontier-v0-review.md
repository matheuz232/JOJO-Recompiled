# Plan self-review — PS1 Kernel BIOS + GP1 + GTE Transfer Frontier v0

Reviewed against `docs/superpowers/specs/2026-09-10-ps1-kernel-gpu-gte-frontier-v0-design.md`.

- Spec coverage: complete across kernel BIOS, GP1, GTE CTC2/CFC2, MAX3 hashing, strict boundaries, CI/artifact verification.
- Placeholder scan: no TODO/TBD placeholders.
- Type/signature consistency: `Ps1HleBios::dispatch(call, cpu, bus)`, `Ps1GpuState`, `R3000aCop2Gte`, and MAX3 hash ownership are internally consistent.
- Mandatory execution clarification: when `Ps1MemoryBus::write32(0x1F801814, value)` sees a known GP1 port but an unsupported GP1 opcode, it must set `last_unsupported_access()` and return `R3000aBusStatus::unsupported`; it must not fall through to diagnostic MMIO shadow.
