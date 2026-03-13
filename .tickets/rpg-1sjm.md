---
id: rpg-1sjm
status: open
deps: [rpg-dzgo]
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-rkj9
---
# §1.4 Object Mode

See ref/scene_editor_design.md §1.4. Left-click select (raycast), shift-click add/remove, box select. A/Shift+A select/deselect all. G/R/S grab/rotate/scale with axis constraint. X/Delete for delete. D for duplicate. H/Shift+H hide/show. Ctrl+right-click 3D cursor. Transform gizmo interaction. Grid snap. Pivot manipulation (Alt+G, :pivot commands).

## Acceptance Criteria

All object mode operations work: select, transform, duplicate, hide/show, snap, pivot. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:09:34Z**

Scope clarification: G/R/S grab/rotate/scale with axis constraint (x/y/z to lock) belongs here in Phase 1. This includes basic server-round-trip transform. Phase 5 grab mode (rpg-vdqf) adds client-side provisional positioning for zero-latency feedback, which is a polish enhancement on top of the Phase 1 implementation.
