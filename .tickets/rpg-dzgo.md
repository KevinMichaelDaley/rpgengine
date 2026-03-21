---
id: rpg-dzgo
status: closed
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-rkj9
---
# §1.1 Viewport Rendering

See ref/scene_editor_design.md §1.1. Camera controls (orbit, pan, zoom), numpad views, grid rendering, 3D cursor, entity rendering from replication snapshot, selection outline, transform gizmos.

## Acceptance Criteria

Entities visible in viewport with proper transforms. Camera orbit/pan/zoom works. Grid renders. Gizmos appear. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:09:26Z**

Scope clarification: camera view shortcuts (front/right/top/ortho toggle) belong here in Phase 1, not Phase 5. Use non-numpad bindings: 1=front, 3=right, 7=top, 5=ortho toggle (number row, not numpad). Smooth transition animation (~200ms lerp) also belongs here.
