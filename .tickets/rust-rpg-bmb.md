---
id: rust-rpg-bmb
status: closed
deps: []
links: []
created: 2026-02-03T00:34:16.000896627-08:00
type: task
priority: 2
---
# Job system: optional queue contention diagnostics

Add compile-time gated job queue contention diagnostics (no overhead when disabled). Provide API to snapshot/reset stats and print them in p000 job performance benchmark when enabled. Add a unit test that passes with or without diagnostics enabled.


