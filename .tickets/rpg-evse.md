---
id: rpg-evse
status: open
deps: [rpg-5h7o]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-hvyg
tags: [aegis, vm, security]
---
# Aegis wall-time backstop enforcement

Implement the wall-time backstop per ref/aegis_bytecode_spec.md §6.1.

The wall-time backstop is a secondary enforcement mechanism (fuel metering is primary). It catches pathological cases where individual instructions take unexpectedly long.

Implement:
- Record clock_gettime(CLOCK_MONOTONIC) before aegis_execute/aegis_resume
- Every N instructions (configurable, default 256), check elapsed wall-time
- If elapsed > wall_time_budget_ns (configurable, default 1 ms): force-yield and flag script
- Wall-time check interval configurable via aegis_config_t
- Flag stored on script instance for admin review

Must not add significant overhead to the hot path (one clock_gettime every 256 instructions is ~4 syscalls/ms at typical instruction rates).

Files:
- src/aegis/aegis_walltime.c
- tests/aegis/aegis_walltime_tests.c

Acceptance criteria:
- [ ] Wall-time budget exceeded triggers force-yield
- [ ] Budget not exceeded allows normal execution
- [ ] Check interval is configurable
- [ ] Script flagged for review after wall-time force-yield
- [ ] Tests: budget exceeded with busy loop, budget OK with fast program, configurable interval

## Acceptance Criteria

Wall-time backstop force-yields on budget exceeded, configurable, flags script

