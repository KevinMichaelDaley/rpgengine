---
id: rpg-g169
status: open
deps: [rpg-nb5l]
links: []
created: 2026-03-02T18:39:34Z
type: task
priority: 2
assignee: KMD
---
# Phase 5c: ragdoll system with per-bone activation

Create per-bone ragdoll activation system with smooth animation-to-physics transition. See ref/renderer_spec.md §6.5-6.6.

Deliverables:
- src/renderer/anim/anim_ragdoll.c: Per-bone ragdoll activation (each bone maps to a physics body with convex collider from convex decomposition of weighted mesh segment)
- Smooth transition: blend from animation pose to physics-driven pose over configurable duration
- Individual bones can ragdoll independently (e.g., limp arm while rest of body is animated)
- Dynamic CCD for fast bones (weapon sweeps): bone velocity from frame-to-frame delta, auto-CCD when displacement > 0.5 × bone_collider_radius, hit events for gameplay
- Convex decomposition per bone uses existing convex_decompose.c/convex_acd.c infrastructure
- Tests in tests/p004_renderer_ragdoll_tests.c

Depends on: rpg-nb5l (constraints provide the XPBD integration layer)

