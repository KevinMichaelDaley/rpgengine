---
id: rust-rpg-hpo
status: closed
deps: []
links: []
created: 2026-02-01T23:25:51.753952074-08:00
type: task
priority: 1
---
# Replace ucontext with ARM context switch

Replace POSIX ucontext-based fibers with a custom ARM context switching backend.

Scope:
- Add job context abstraction (src/job/context.h, src/job/context.c) with job_context_init and job_context_swap.
- AArch64/ARM uses a custom assembly swap that preserves callee-saved GPRs and AArch64 SIMD callee-saved regs (v8–v15).
- Non-ARM platforms fall back to ucontext.
- Wire job system to use job_context_t instead of ucontext_t (fiber create/yield, scheduler swap).

Acceptance:
- ./build/p000_tests passes.
- make -B test_p008 passes.
- No new warnings on ARM builds.


