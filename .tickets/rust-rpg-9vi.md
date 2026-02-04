---
id: rust-rpg-9vi
status: in_progress
deps: []
links: []
created: 2026-02-01T23:05:42.507987786-08:00
type: task
priority: 2
---
# Fiber-safety audit: remove TLS + heap alloc from non-IO modules

Audit all modules under src/ to ensure fiber-safety and non-allocating behavior.

Scope:
- Include: src/** EXCEPT src/net/** (allowed to malloc because it runs on IO thread) and any OpenGL/renderer code paths (GL not thread-safe; handled via render/command-queue thread).
- Focus on code that can run on fibers (job system scheduled work).

Acceptance criteria:
- No thread-local storage usage in scoped modules: no `_Thread_local`, `__thread`, `thread_local`, `pthread_key_*`.
- No heap allocation/free in scoped modules: no `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc` (or equivalents).
- Where dynamic memory is required, APIs accept an arena/pool (passed in explicitly) or caller-provided storage; ownership and lifetimes must be documented.
- Provide a report of offenders (file + function) and land fixes incrementally with minimal behavior changes.

Notes:
- Networking code (src/net/**) may use malloc/free because it runs on an IO thread.
- Renderer/OpenGL work should be queued and executed on the render thread; do not attempt to make GL calls fiber-safe.

First milestone:
- Repo-wide scan and categorization of offenders into (A) easy mechanical fixes, (B) API changes needed, (C) intentionally allowed (net/GL excluded).


