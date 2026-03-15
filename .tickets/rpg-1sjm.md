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

See ref/scene_editor_design.md §1.4. Left-click select (raycast), shift-click add/remove, box select. A/Shift+A select/deselect all. X/Delete for delete. D for duplicate. H/Shift+H hide/show. Ctrl+right-click 3D cursor. Transform gizmo interaction. Pivot manipulation (Alt+G, :pivot commands).

## Grid Snap Modes

- **Position snap**: quantize to grid spacing (per-axis on/off)
- **Rotation snap**: quantize to angle increment
- **Scale snap**: quantize to scale increment
- **Snap to face**: snap object to nearest face, orientation derived from face normal
- **Snap to vertex**: snap object to nearest vertex, orientation derived from vertex normal
- **Snap on surface**: like snap-to-face/vertex but offset along normal so object rests on surface without collision
- **Snap to constraint**: edit-time rigid body constraints (positional anchors, distance limits) enforced locally using solver subset; derives absolute position/orientation sent to server
- **Constrained movement**: move objects while respecting active snap constraints (may use local solver to derive final position/orientation update sent to server)

## Acceptance Criteria

All object mode operations work: select, transform, duplicate, hide/show, snap, pivot. All snap modes produce correct absolute transforms sent to server. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

## Notes

**2026-03-14T00:00:00Z**

Scope change: Removed G/R/S keyboard transforms — existing gizmo modes are sufficient. Expanded grid snap section with snap-to-face, snap-to-vertex, snap-on-surface, and snap-to-constraint modes. All snap operations derive absolute position/orientation locally and send to server.
