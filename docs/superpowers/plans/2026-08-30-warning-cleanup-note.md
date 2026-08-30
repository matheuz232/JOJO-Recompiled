# Compiler Warning Cleanup

This temporary audit note records warning classes found while hardening M9 and the wider core. Warnings are fixed at their source rather than suppressed globally.

Open classes observed in Linux/MSVC CI include integer narrowing in little-endian decoders, ignored `[[nodiscard]]` test results, MSVC CRT environment access in a test fixture, obsolete unused FPU helpers, and an always-true native-x64 offset assertion.

This note is not a completion claim; it exists so the warning sweep remains explicit while fixes are verified.
