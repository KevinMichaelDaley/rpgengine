---
id: rust-rpg-00t
status: closed
deps: []
links: []
created: 2026-02-01T23:45:13.701909701-08:00
type: task
priority: 1
---
# x86_64 fiber context: save SIMD state

Add an x86_64 SysV assembly backend for job fibers that preserves SIMD state.

Requirements:
- Correct stack-correct context switching (no stack drift).
- Preserve x87/MMX/XMM via FXSAVE/FXRSTOR.
- If compiled with AVX (__AVX__), also preserve YMM0-15.
- If compiled with AVX-512 (__AVX512F__), also preserve ZMM0-31 + opmask k0-k7.

Acceptance:
- Job tests pass on existing arch.
- Deploy test can run on the x86_64 VM used for client/server integration.


