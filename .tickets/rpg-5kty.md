---
id: rpg-5kty
status: open
deps: []
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-o25i
---
# §6.2 Keyframe System

See ref/scene_editor_design.md §6.2. All 17 channel types from spec §6.6. keyframe_store_t per-entity. Insert/delete/modify keyframes. Interpolation (step/linear/cubic Bezier with handles). Extrapolation modes (constant/linear/cycle). Server sync via anim_keyframe commands.

## Acceptance Criteria

Keyframe store works. All interpolation modes correct. Server sync functional. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

