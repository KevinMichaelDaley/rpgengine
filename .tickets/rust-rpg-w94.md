---
id: rust-rpg-w94
status: closed
deps: []
links: []
created: 2026-02-02T00:04:13.821326338-08:00
type: task
priority: 1
---
# x86_64 fiber context: seed FXSAVE state

Fix x86_64 fiber bootstrap to initialize the FXSAVE image for new contexts.

Why:
- New fibers were created with a zeroed FXSAVE area.
- First `fxrstor64` could restore MXCSR with exceptions unmasked, potentially causing SIGFPE in SIMD-heavy code (e.g., networking/math).

Acceptance:
- `build/p000_tests` passes locally.
- Remote `p000_tests` passes on the x86_64 VM.
- `tools/deploy_net_integration.sh --test p008 --clients 4` no longer exits early due to FP exceptions.


