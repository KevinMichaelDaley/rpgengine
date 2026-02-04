---
id: rust-rpg-o4i.5.2
status: closed
deps: [rust-rpg-o4i.5.1]
links: []
created: 2026-01-18T11:04:31.021100443-08:00
type: task
priority: 2
parent: rust-rpg-o4i.5
---
# P_004.5.b Implementation - bone palette buffers

## P_004.5.b Implementation - bone palette buffers

### Goal
Implement palette buffers with SSBO/UBO/TBO capability gating.

### Scope
- Capability checks select buffer path.
- Persistent buffers; no per-frame mallocs.
- Binding per draw and size limit handling.



