---
id: rpg-qthg
status: in_progress
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


## Notes

**2026-07-16T06:52:49Z**

Storage + lookup accel done (TDD, gi_probe_tests pass): gi_probe_set (adaptive probes over caller backing -- pos[3*cap] + sh[27*cap], add/reset) and gi_probe_grid (uniform CSR bin grid over an AABB: build counting-sort, cell(), gather() over the query cell + 26 neighbours, cap-respecting). Pure CPU, uploads as SSBOs. REMAINING for this ticket: adaptive placement/seeding of probe positions (near surfaces/play space) -- will add when wiring the demo/kernel.
