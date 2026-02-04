---
id: rust-rpg-o4i.5
status: closed
deps: [rust-rpg-o4i.4]
links: []
created: 2026-01-18T10:58:29.234895661-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.5 Bone palette buffers + capability gating

## P_004.5 Bone palette buffers + capability gating

### Goal
Implement bone palette buffer creation/update with SSBO/UBO/TBO fallback paths.

### Scope
- Capability checks select SSBO/UBO/TBO path.
- Create/update palette buffers without per-frame mallocs.
- Binding per draw uses explicit binding points.
- Palette size limits handled (error or split; documented).

### Tests
- Bone palette upload/bind uses correct GL calls.
- Fallback path engaged when SSBO unsupported.
- Palette size limit behavior.



