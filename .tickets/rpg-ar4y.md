---
id: rpg-ar4y
status: open
deps: []
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-c55w
---
# §3.1 Mesh Mode

See ref/scene_editor_design.md §3.1. Selection mode toggle (vertex/edge/face/polygroup), element selection (click/box/loop/linked), mesh tools (extrude/bevel/inset/subdivide/loop cut/merge), mesh tool gizmos, server round-trip, collision mesh target toggle (Shift+C), collision mesh visualization (green wireframe vs ghost), collision mesh creation/auto-generation/clear, inspector integration.

## Acceptance Criteria

Mesh editing works: select verts/edges/faces, extrude, bevel, subdivide. Collision mesh: toggle, edit separately, auto-generate. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

