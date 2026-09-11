# OMEGA Infinity Diagnostic Continuations Addendum

This addendum is binding together with `2026-09-11-ps1-omega-infinity-total-evidence-design.md`.

## Purpose

OMEGA Infinity must be able to discover later blockers after a strict terminal MMIO write without implementing the missing hardware behavior.

## Terminal MMIO write continuation

When Infinity reaches an unsupported MMIO write on a path that is otherwise expandable, it may create exactly one diagnostic continuation candidate named `accept_write_no_effect`.

The continuation:

- is available only to Infinity/diagnostic exploration, never normal runtime execution;
- records the original address, width, value, PC and opcode as a dependency/frontier;
- advances the CPU as if the store instruction retired successfully;
- performs no write to device state or diagnostic MMIO shadow;
- immediately changes the descendant evidence class to `speculative`;
- appends an explicit `Ps1Max3Decision`/assumption entry describing the no-effect write;
- cannot replace a strict best node/report;
- cannot produce an authoritative commercial-frame claim;
- is deterministic and replayable.

A strict path always stops at the unsupported write. Only the speculative descendant crosses it.

## Other terminal classes

This addendum does not authorize generic no-op continuation for unsupported CPU instructions, GPU commands, CD-ROM commands, BIOS calls or fatal runtime errors. Those require class-specific diagnostic candidates or future approved addenda.

## Current commercial relevance

The strict JoJo frontier at baseline is `write8 0x1F801802 = 0x07` at PC `0x8004B4A4`. Infinity must preserve that node as the strict authority while allowing a speculative `accept_write_no_effect` descendant so later frontiers can be observed in the same session.
