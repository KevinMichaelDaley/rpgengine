---
id: rust-rpg-o4i.8
status: closed
deps: [rust-rpg-o4i.3, rust-rpg-o4i.6, rust-rpg-o4i.7]
links: []
created: 2026-01-18T10:58:29.629141174-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.8 ECS + job integration for skinning

## P_004.8 ECS + job integration for skinning

### Goal
Integrate ECS skeleton/skin components and job-based CPU evaluation for bone matrices.

### Scope
- Define skeleton/skin components referencing bone transforms.
- Job pipeline computes bone matrices and writes to per-entity palettes.
- Stable palette index mapping; GPU uploads ordered deterministically.

### Tests
- Bone palette index mapping stability.
- GPU skinning path integration test.
- Render command generation loop with deterministic GL call sequence.



