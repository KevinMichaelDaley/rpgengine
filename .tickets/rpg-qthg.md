---
id: rpg-qthg
status: open
deps: []
links: []
created: 2026-07-16T06:47:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-fo9r
---
# Adaptive irradiance probe set: stored positions + SH9 storage + lookup accel grid

Probes have EXPLICIT stored positions (adaptive placement, NOT a grid). Storage: array of { vec3 pos, SH9 rgb }. Provide seeding/placement (near surfaces/play space -- start simple, e.g. sample above the floor / around geometry) and a coarse uniform ACCELERATION grid (cell -> probe index list) so the shader can gather nearby probes. TDD the host-side buffers + accel binning.

